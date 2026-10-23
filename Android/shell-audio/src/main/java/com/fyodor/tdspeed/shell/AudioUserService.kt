package com.fyodor.tdspeed.shell

import android.content.Context
import android.content.pm.PackageManager
import android.os.Binder
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.os.Process
import android.os.SystemClock
import androidx.annotation.Keep
import com.fyodor.tdspeed.nativebridge.NativeBridge
import com.fyodor.tdspeed.protocol.IAudioBridge
import com.fyodor.tdspeed.protocol.Protocol
import com.fyodor.tdspeed.protocol.SessionLease
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

@Keep
class AudioUserService @Keep constructor(private val context: Context) : IAudioBridge.Stub() {
    private val expectedUid = context.packageManager.getApplicationInfo(context.packageName, 0).uid
    private val control = Executors.newSingleThreadExecutor()
    private val watchdog = Executors.newSingleThreadScheduledExecutor()
    private val lease = SessionLease()
    private val lock = Any()
    @Volatile private var pipeline: DiagnosticPipeline? = null
    @Volatile private var state = "STOPPED"
    @Volatile private var errorDetail = ""
    @Volatile private var nativeIdentity = "not loaded"
    private var owner: IBinder? = null
    private val ownerDeath = IBinder.DeathRecipient { enqueueStop("CONTROLLER_DIED") }
    private var lastAudio = Bundle()
    @Volatile private var stopRequestedAt = 0L
    private var completedSession = 0L

    init {
        watchdog.scheduleAtFixedRate({
            val now = SystemClock.elapsedRealtime()
            val expired = synchronized(lock) { lease.expired(now) }
            if (expired) enqueueStop("CONTROLLER_LEASE_EXPIRED")
            pipeline?.let { if (now - it.progressAt >= Protocol.TRANSPORT_TIMEOUT_MS) enqueueStop("TRANSPORT_STALLED") }
            // A blocked control executor must not be able to defeat the watchdog itself.
            if (stopRequestedAt != 0L && now - stopRequestedAt >= 5_000) Process.killProcess(Process.myPid())
        }, 1, 1, TimeUnit.SECONDS)
    }
    private fun authorize() {
        check(Binder.getCallingUid() == expectedUid) { "UNAUTHORIZED_CALLER" }
        check(Process.myUid() == 2000) { "WRONG_BACKEND_UID" }
    }
    override fun probe(protocolVersion: Int): Bundle {
        authorize(); require(protocolVersion == Protocol.VERSION) { "PROTOCOL_MISMATCH" }
        return Bundle().apply {
            putInt("protocolVersion", Protocol.VERSION); putInt("helperUid", Process.myUid()); putInt("apiLevel", Build.VERSION.SDK_INT)
            putInt("ownerUid", expectedUid); putString("buildFingerprint", Build.FINGERPRINT)
            for (permission in listOf("MODIFY_AUDIO_ROUTING", "CAPTURE_AUDIO_OUTPUT", "MEDIA_CONTENT_CONTROL")) {
                putBoolean(permission, context.checkPermission("android.permission.$permission", Process.myPid(), Process.myUid()) == PackageManager.PERMISSION_GRANTED)
            }
            val result = runCatching { nativeIdentity = NativeBridge.identity(); nativeIdentity }
            putBoolean("nativeLoaded", result.isSuccess); putString("nativeIdentity", result.getOrElse { describe(it) })
            putBoolean("policyApiPresent", runCatching { Class.forName("android.media.audiopolicy.AudioPolicy") }.isSuccess)
            putString("qualification", "P2 pitch alpha; RAM history, phone route only; car handling is unqualified")
        }
    }
    override fun start(sessionId: Long, commandId: Long, owner: IBinder) {
        startConfigured(sessionId, commandId, owner, Bundle())
    }
    override fun startConfigured(sessionId: Long, commandId: Long, owner: IBinder, options: Bundle) {
        authorize()
        val allowCarCapture = options.getBoolean("allowCarCapture", false)
        synchronized(lock) { lease.begin(sessionId, commandId, SystemClock.elapsedRealtime()); state = "PREPARING"; errorDetail = "" }
        try { owner.linkToDeath(ownerDeath, 0); this.owner = owner }
        catch (error: Throwable) { synchronized(lock) { lease.end() }; state = "ERROR"; throw error }
        control.execute {
            try {
                check(context.checkPermission("android.permission.MODIFY_AUDIO_ROUTING", Process.myPid(), Process.myUid()) == PackageManager.PERMISSION_GRANTED) { "PERMISSION_DENIED: MODIFY_AUDIO_ROUTING" }
                val created = DiagnosticPipeline(context, expectedUid, allowCarCapture) { error -> enqueueStop("AUDIO_FAILURE: ${describe(error)}") }
                pipeline = created; created.start(); state = "RUNNING"
            } catch (error: Throwable) { stopInternal("START_FAILED: ${describe(error)}") }
        }
    }
    override fun collectRouteReport(): Bundle {
        authorize()
        return RouteReport.collect(context, snapshot())
    }
    override fun heartbeat(sessionId: Long) { authorize(); synchronized(lock) { lease.heartbeat(sessionId, SystemClock.elapsedRealtime()) } }
    override fun stop(sessionId: Long, commandId: Long) {
        authorize()
        val accepted = synchronized(lock) { if (!lease.active && sessionId == completedSession) false else lease.accept(sessionId, commandId) }
        if (accepted) control.execute { stopInternal("") }
    }
    override fun setDiagnostic(sessionId: Long, commandId: Long, gain: Float, swap: Boolean) {
        authorize(); require(gain.isFinite() && gain in 0f..1f)
        val accepted = synchronized(lock) { lease.accept(sessionId, commandId) }
        if (accepted) control.execute { pipeline?.diagnostic(gain, swap) }
    }
    override fun setListening(sessionId: Long, commandId: Long, semitones: Float, recenter: Boolean) {
        authorize(); require(semitones.isFinite() && semitones in -12f..12f)
        val accepted = synchronized(lock) { lease.accept(sessionId, commandId) }
        if (accepted) control.execute { pipeline?.listening(semitones, recenter, commandId) }
    }
    override fun snapshot(): Bundle {
        authorize()
        return Bundle().apply {
            putAll(pipeline?.snapshot() ?: lastAudio)
            putString("state", state); putString("error", errorDetail); putString("nativeIdentity", nativeIdentity)
            putBoolean("policyRegistered", pipeline?.policyRegistered == true)
            putInt("protocolVersion", Protocol.VERSION); putInt("helperUid", Process.myUid())
            synchronized(lock) { putLong("sessionId", lease.sessionId); putLong("lastCommandId", lease.lastCommand) }
            putLong("elapsedRealtimeMs", SystemClock.elapsedRealtime())
        }
    }
    private fun enqueueStop(reason: String) {
        if (state == "STOPPED" || state == "ERROR") return
        if (stopRequestedAt == 0L) stopRequestedAt = SystemClock.elapsedRealtime()
        runCatching { control.execute { if (state != "STOPPED" && state != "ERROR") stopInternal(reason) } }
    }
    private fun stopInternal(reason: String) {
        state = "STOPPING"
        pipeline?.let { lastAudio = it.snapshot() }
        val cleanup = runCatching { pipeline?.stop() }.exceptionOrNull(); pipeline = null
        runCatching { owner?.unlinkToDeath(ownerDeath, 0) }; owner = null
        synchronized(lock) { completedSession = lease.sessionId; lease.end() }
        stopRequestedAt = 0
        errorDetail = listOfNotNull(reason.takeIf { it.isNotBlank() }, cleanup?.let { "CLEANUP_INCOMPLETE: ${describe(it)}" }).joinToString("\n")
        state = if (errorDetail.isEmpty()) "STOPPED" else "ERROR"
        // Framework policy-owner death is the last restoration path after a failed cleanup.
        if (cleanup != null) { watchdog.shutdownNow(); Process.killProcess(Process.myPid()) }
    }
    override fun destroy() {
        val caller = Binder.getCallingUid()
        check(caller == expectedUid || caller == 2000 || caller == 1000) { "UNAUTHORIZED_CALLER" }
        control.execute { stopInternal(""); watchdog.shutdownNow(); Process.killProcess(Process.myPid()) }
    }
    companion object {
        private fun describe(error: Throwable): String {
            var cause = error
            while (cause.cause != null && cause.cause !== cause) cause = cause.cause!!
            return "${cause.javaClass.simpleName}: ${cause.message}"
        }
    }
}
