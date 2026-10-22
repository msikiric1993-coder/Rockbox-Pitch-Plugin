package com.fyodor.tdspeed.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentName
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Bundle
import android.os.IBinder
import android.os.PowerManager
import android.os.SystemClock
import com.fyodor.tdspeed.protocol.IAudioBridge
import com.fyodor.tdspeed.protocol.Protocol
import com.fyodor.tdspeed.shell.AudioUserService
import org.json.JSONArray
import org.json.JSONObject
import rikka.shizuku.Shizuku
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class BridgeService : Service() {
    inner class LocalBinder : Binder() { val service get() = this@BridgeService }
    private val binder = LocalBinder()
    private val owner = Binder()
    private val executor = Executors.newSingleThreadScheduledExecutor()
    @Volatile private var remote: IAudioBridge? = null
    private var binding = false
    private var pendingStart = false
    private var pendingStop = false
    private var foreground = false
    private var session = 0L
    private var command = 0L
    private var lastHeartbeat = 0L
    private var probeData = Bundle()
    private var wake: PowerManager.WakeLock? = null
    private var wakeRenewAt = 0L
    private val args by lazy { Shizuku.UserServiceArgs(ComponentName(this, AudioUserService::class.java))
        .daemon(false).processNameSuffix("audio_helper").debuggable(BuildConfig.DEBUG).version(BuildConfig.VERSION_CODE) }
    private val death = Shizuku.OnBinderDeadListener { executor.execute { disconnected("SHIZUKU_UNAVAILABLE: service stopped") } }
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, service: IBinder) {
            executor.execute {
                remote = IAudioBridge.Stub.asInterface(service); binding = false
                try {
                    probeData = remote!!.probe(Protocol.VERSION)
                    val good = probeData.getBoolean("MODIFY_AUDIO_ROUTING") && probeData.getBoolean("nativeLoaded") && probeData.getInt("helperUid") == 2000
                    publish(if (good) "READY" else "UNSUPPORTED", "Capability check complete. Routing is not yet verified.", probeData)
                    if (pendingStart && good) beginSession()
                    else if (pendingStart) { pendingStart = false; leaveForeground() }
                } catch (error: Throwable) { failed(error) }
            }
        }
        override fun onServiceDisconnected(name: ComponentName) { runCatching { executor.execute { disconnected("HELPER_DISCONNECTED") } } }
    }

    override fun onCreate() {
        super.onCreate()
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL, "Varispeed playback", NotificationManager.IMPORTANCE_LOW))
        Shizuku.addBinderDeadListener(death)
        executor.scheduleWithFixedDelay({ poll() }, 250, 250, TimeUnit.MILLISECONDS)
    }
    override fun onBind(intent: Intent): IBinder = binder
    override fun dump(fd: java.io.FileDescriptor, writer: java.io.PrintWriter, args: Array<out String>) {
        writer.println(export(LabModel.state.value))
    }
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                startForeground(NOTIFICATION, notification("Listening · tap to open controls"), ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
                foreground = true
                executor.execute { pendingStart = true; if (remote != null) beginSession() else connectInternal() }
            }
            ACTION_STOP -> executor.execute { stopSession() }
        }
        return START_NOT_STICKY
    }
    fun inspect() { executor.execute { if (remote == null) connectInternal() else inspectConnected() } }
    private fun connectInternal() {
        if (binding) return
        try {
            check(Shizuku.pingBinder()) { "SHIZUKU_UNAVAILABLE: start Shizuku first" }
            check(Shizuku.getUid() == 2000) { "WRONG_BACKEND_UID: start Shizuku with ADB, without root" }
            binding = true; publish("CONNECTING", "Binding the shell helper")
            Shizuku.bindUserService(args, connection)
        } catch (error: Throwable) { binding = false; failed(error) }
    }
    private fun inspectConnected() {
        try { probeData = remote!!.probe(Protocol.VERSION); publish("READY", "Probe complete. No audio route is active.", probeData) }
        catch (error: Throwable) { failed(error) }
    }
    private fun beginSession() {
        if (session != 0L) { pendingStart = false; return }
        try {
            session = SystemClock.elapsedRealtimeNanos().coerceAtLeast(1); command = 1; pendingStart = false; pendingStop = false
            remote!!.start(session, command, owner)
            val manager = getSystemService(PowerManager::class.java)
            wake = manager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "TDSpeed:stereo-forwarding").apply { setReferenceCounted(false); acquire(600_000) }
            wakeRenewAt = SystemClock.elapsedRealtime() + 540_000
            lastHeartbeat = SystemClock.elapsedRealtime()
            publish("PREPARING", "Preparing the replacement audio route", probeData)
        } catch (error: Throwable) { failed(error) }
    }
    fun diagnostic(gain: Float, swap: Boolean) {
        executor.execute {
            if (session == 0L) return@execute
            try { remote?.setDiagnostic(session, ++command, gain, swap) } catch (error: Throwable) { failed(error) }
        }
    }
    fun listening(semitones: Float, recenter: Boolean = false) {
        executor.execute {
            if (session == 0L || pendingStop) return@execute
            try { remote?.setListening(session, ++command, semitones, recenter) }
            catch (error: Throwable) { failed(error) }
        }
    }
    private fun poll() {
        if (session == 0L) return
        try {
            val bridge = remote ?: return
            val now = SystemClock.elapsedRealtime()
            // Stop is asynchronous. Do not renew a lease that the helper may
            // already have ended while we wait for its cleanup acknowledgement.
            if (!pendingStop && now - lastHeartbeat >= Protocol.HEARTBEAT_MS) { bridge.heartbeat(session); lastHeartbeat = now }
            if (now >= wakeRenewAt) { wake?.acquire(600_000); wakeRenewAt = now + 540_000 }
            val snapshot = bridge.snapshot()
            val status = snapshot.getString("state") ?: "UNKNOWN"
            val combined = Bundle(probeData).apply { putAll(snapshot) }
            publish(status, snapshot.getString("error").orEmpty().ifBlank { snapshot.getString("signalStatus") ?: "Waiting for source audio" }, combined)
            if (status == "ERROR" || status == "STOPPED") { session = 0; leaveForeground() }
        } catch (error: Throwable) { failed(error) }
    }
    private fun stopSession() {
        pendingStart = false
        try {
            if (session != 0L) { pendingStop = true; remote?.stop(session, ++command) }
            // Poll until the helper has acknowledged cleanup; do not report STOPPED prematurely.
            if (session == 0L) { publish("STOPPED", "No forwarding session is active", probeData); leaveForeground() }
        } catch (error: Throwable) { failed(error) }
    }
    private fun failed(error: Throwable) {
        val message = "${error.javaClass.simpleName}: ${error.message}"
        // Removing the UserService is a fallback that also releases framework-owned policy state.
        runCatching { Shizuku.unbindUserService(args, connection, true) }
        remote = null; session = 0; binding = false; pendingStart = false
        publish("ERROR", message, probeData); leaveForeground()
    }
    private fun disconnected(message: String) {
        remote = null; binding = false; session = 0; pendingStart = false
        publish("DISCONNECTED", message, probeData); leaveForeground()
    }
    private fun leaveForeground() {
        wake?.let { if (it.isHeld) it.release() }; wake = null
        if (foreground) { stopForeground(STOP_FOREGROUND_REMOVE); foreground = false; stopSelf() }
    }
    private fun publish(status: String, message: String, data: Bundle = Bundle()) {
        LabModel.state.value = LabState(status, message, Bundle(data))
    }
    private fun notification(message: String): Notification {
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, BridgeService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE)
        return Notification.Builder(this, CHANNEL).setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Varispeed").setContentText(message).setContentIntent(open)
            .setOngoing(true).addAction(Notification.Action.Builder(null, "Stop", stop).build()).build()
    }
    override fun onDestroy() {
        Shizuku.removeBinderDeadListener(death)
        executor.execute {
            runCatching { if (session != 0L) remote?.stop(session, ++command) }
            runCatching { Shizuku.unbindUserService(args, connection, true) }
            wake?.let { if (it.isHeld) it.release() }; wake = null
        }
        executor.shutdown()
        super.onDestroy()
    }
    companion object {
        const val ACTION_START = "com.fyodor.tdspeed.START"
        const val ACTION_STOP = "com.fyodor.tdspeed.STOP"
        private const val CHANNEL = "routing-lab"
        private const val NOTIFICATION = 1
        fun export(state: LabState): String {
            val result = JSONObject().put("recordType", "p0_diagnostic").put("protocolVersion", Protocol.VERSION)
                .put("status", state.status).put("message", state.message).put("compatibilityResult", "not_qualified")
            val details = JSONObject()
            for (key in state.data.keySet()) {
                @Suppress("DEPRECATION") val value = state.data.get(key)
                details.put(key, if (value is List<*>) JSONArray(value) else value ?: JSONObject.NULL)
            }
            result.put("details", details)
            return result.toString(2)
        }
    }
}
