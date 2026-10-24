package com.fyodor.tdspeed.shell

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioManager
import android.os.Looper
import android.os.Process
import android.os.SystemClock
import org.json.JSONObject
import kotlin.system.exitProcess
import com.fyodor.tdspeed.protocol.PolicyMixSnapshot
import com.fyodor.tdspeed.protocol.ProjectionDiscovery

/** No PCM. Only the verified projection MEDIA/GAME mix is amended; navigation is untouched. */
internal object CarRouteGuardian {
    private data class Saved(val mix: Any, val original: Any, var installed: String, var sourceUids: Set<Int> = emptySet())
    @JvmStatic fun main(args: Array<String>) {
        var code = 0
        val saved = linkedMapOf<String, Saved>()
        val restored = linkedMapOf<String, Saved>()
        var audioManager: AudioManager? = null
        var lastStatus = ""
        var discoveryEvidence = "none"
        fun publish(status: String, detail: String = "", uids: Set<Int> = emptySet()) {
            val json = JSONObject().put("status", status).put("detail", detail.take(300)).put("excluded", uids.sorted().joinToString(","))
                .put("discovery", discoveryEvidence).toString()
            if (json != lastStatus) { println("CAR_ROUTE_STATUS=$json"); lastStatus = json }
        }
        fun restore(address: String, mixes: List<PolicyMixSnapshot>) {
            val entry = saved.getValue(address)
            val current = mixes.singleOrNull { it.address == address }
            // Do not overwrite changes made by another owner, or resurrect a disconnected mix.
            if (current != null && current.signature == entry.installed) {
                NativeMixRules.update(entry.mix, entry.original)
                restored[address] = entry
            }
            saved.remove(address)
        }
        try {
            check(Process.myUid() == 2000)
            require(args.size == 2 || args.size == 4 || args.size == 5)
            if (Looper.getMainLooper() == null) Looper.prepareMainLooper()
            val activity = Class.forName("android.app.ActivityThread")
            val thread = activity.getMethod("systemMain").invoke(null)
            val system = activity.getMethod("getSystemContext").invoke(thread) as Context
            val context = system.createPackageContext(args[0], Context.CONTEXT_INCLUDE_CODE or Context.CONTEXT_IGNORE_SECURITY)
            ShellAttribution.install(context)
            val ownerUid = args[1].toInt()
            val testing = args.size >= 4
            val replay = args.getOrNull(4)
            require(replay == null || replay in setOf("seed", "rolled"))
            val projectionUid = if (testing) 42424 else runCatching { context.packageManager.getApplicationInfo("com.google.android.projection.gearhead", 0).uid }.getOrDefault(-1)
            val manager = context.getSystemService(AudioManager::class.java)
            audioManager = manager
            val store = ProjectionIdentityStore(context, ownerUid, projectionUid, if (testing) args[2] else null)
            val discovery = ProjectionDiscovery(store.load())
            val stopped = java.util.concurrent.atomic.AtomicBoolean(false)
            val ping = java.util.concurrent.atomic.AtomicLong(SystemClock.elapsedRealtime())
            val captureAddress = java.util.concurrent.atomic.AtomicReference("")
            Thread({
                try { System.`in`.bufferedReader().useLines { lines ->
                    for (line in lines) {
                        if (line == "STOP") break
                        if (line == "PING") ping.set(SystemClock.elapsedRealtime())
                        if (line.startsWith("CAPTURE ") && line.length <= 264) captureAddress.set(line.substring(8))
                    }
                } } finally { stopped.set(true) }
            }, "Varispeed-car-lease").apply { isDaemon = true; start() }
            var addresses = emptySet<String>(); var identityAt = 0L
            while (!stopped.get() && SystemClock.elapsedRealtime() - ping.get() < 6000) {
                if (projectionUid < 0) { publish("NO_ANDROID_AUTO"); Thread.sleep(500); continue }
                // Phone/headphone sessions need no repeated policy dumps while projection is absent.
                if (!testing && saved.isEmpty() && captureAddress.get().isNotEmpty() &&
                    manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).none { it.type == 25 && it.address != captureAddress.get() }) {
                    publish("WAITING_FOR_ANDROID_AUTO"); Thread.sleep(500); continue
                }
                val now = SystemClock.elapsedRealtime()
                val mixes = NativeMixRules.inspect()
                if (testing && replay == null) addresses = setOf(args[2])
                else if (now >= identityAt) {
                    val dump = if (replay == "seed") "registerAudioPolicy for test u/pid:$projectionUid/1 with config:reg:${args[2].substringBefore("mixp:")} Mix:0"
                        else if (replay == "rolled") "registerAudioPolicy for other u/pid:2000/2 with config:reg:other:ap:999 Mix:0"
                        else NativeMixRules.dump("audio")
                    val recorderAddresses = if (testing) emptySet() else runCatching {
                        manager.activeRecordingConfigurations.mapNotNull { config ->
                            val uid = config.javaClass.getMethod("getClientUid").invoke(config) as Int
                            val name = config.javaClass.getMethod("getClientPackageName").invoke(config) as String
                            if (uid == projectionUid && name == "com.google.android.projection.gearhead") config.audioDevice?.address else null
                        }.toSet()
                    }.getOrDefault(emptySet())
                    addresses = discovery.discover(projectionUid, dump, mixes.map { it.address }.toSet(), recorderAddresses)
                    discoveryEvidence = discovery.evidence
                    store.save(addresses)
                    identityAt = now + 2000
                }
                val sources = if (testing) setOf(args[3].toInt()) else manager.activePlaybackConfigurations.mapNotNull { config ->
                    val active = config.javaClass.getMethod("isActive").invoke(config) as Boolean
                    val uid = config.javaClass.getMethod("getClientUid").invoke(config) as Int
                    if (active && config.audioAttributes.usage in setOf(AudioAttributes.USAGE_MEDIA, AudioAttributes.USAGE_UNKNOWN)
                        && uid >= 10000 && uid != ownerUid && uid != projectionUid) uid else null
                }.toSet()
                val targets = mixes.filter { mix -> val addr = mix.address
                    addr in addresses && (saved.containsKey(addr) || mix.projectionMedia) }
                val wanted = NativeMixRules.mediaGameExcept(sources)
                val signature = PolicyMixSnapshot.expected(sources)
                for (address in saved.keys.toList()) if (targets.none { it.address == address } || sources.isEmpty()) restore(address, mixes)
                if (sources.isNotEmpty()) for (mix in targets) {
                    val address = mix.address
                    val previous = saved[address]
                    val current = mix.signature
                    check(previous == null || current == previous.installed) { "PROJECTION_RULE_CHANGED_EXTERNALLY" }
                    if (current != signature) {
                        // Retain restoration state before the native mutation.
                        val entry = previous ?: Saved(NativeMixRules.construct(address), NativeMixRules.mediaGameExcept(emptySet()), current).also { saved[address] = it }
                        NativeMixRules.update(entry.mix, wanted); entry.installed = signature
                        entry.sourceUids = sources.toSet()
                        restored.remove(address)
                    }
                }
                publish(if (saved.isNotEmpty()) "REROUTED" else if (targets.isEmpty()) "WAITING_FOR_ANDROID_AUTO" else "WAITING_FOR_MUSIC",
                    detail = if (testing) mixes.filter { it.address in addresses }.joinToString { "flags=${it.flags} type=${it.type} device=${it.device} rules=${it.signature}" } else "",
                    uids = sources)
                Thread.sleep(500)
            }
            // On Stop or parent death, restoration must follow removal of the capture mix.
            val deadline = SystemClock.elapsedRealtime() + 8000
            while (captureAddress.get().isNotEmpty() && saved.isNotEmpty() &&
                NativeMixRules.inspect().any { it.address == captureAddress.get() }) {
                check(SystemClock.elapsedRealtime() < deadline) { "CAPTURE_RELEASE_TIMEOUT" }
                Thread.sleep(50)
            }
        } catch (error: Throwable) {
            code = 1; publish("ERROR", root(error))
        } finally {
            try {
                val mixes = NativeMixRules.inspect()
                for (address in saved.keys.toList()) restore(address, mixes)
                for ((address, entry) in restored) {
                    val current = NativeMixRules.inspect().singleOrNull { it.address == address } ?: continue
                    check(current.signature == PolicyMixSnapshot.expected(emptySet())) { "RESTORED_RULE_NOT_APPLIED" }
                    fun returnedToCar(): Boolean = audioManager!!.activePlaybackConfigurations.none { config ->
                        val uid = config.javaClass.getMethod("getClientUid").invoke(config) as Int
                        val active = config.javaClass.getMethod("isActive").invoke(config) as Boolean
                        active && uid in entry.sourceUids && config.audioAttributes.usage in
                            setOf(AudioAttributes.USAGE_MEDIA, AudioAttributes.USAGE_GAME) && config.audioDeviceInfo?.address != address
                    }
                    if (!returnedToCar()) {
                        // One bounded refresh after our mix is gone handles a stale selected output.
                        NativeMixRules.update(entry.mix, entry.original)
                        val until = SystemClock.elapsedRealtime() + 1200
                        while (!returnedToCar() && SystemClock.elapsedRealtime() < until) Thread.sleep(50)
                    }
                    check(returnedToCar()) { "SOURCE_DID_NOT_RETURN_TO_CAR" }
                }
                if (code == 0) publish("RESTORED")
            } catch (error: Throwable) { code = 2; publish("ERROR", "RESTORE_FAILED: ${root(error)}") }
        }
        exitProcess(code)
    }
    private fun root(error: Throwable): String {
        var cause = error; while (cause.cause != null && cause.cause !== cause) cause = cause.cause!!
        return "${cause.javaClass.simpleName}: ${cause.message}"
    }
}
