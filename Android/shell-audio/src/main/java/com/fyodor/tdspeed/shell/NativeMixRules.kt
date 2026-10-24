package com.fyodor.tdspeed.shell

import android.media.AudioAttributes
import android.media.AudioFormat
import com.fyodor.tdspeed.protocol.PolicyMixSnapshot
import java.util.concurrent.TimeUnit

/** Shell-only access; callers must retain the original rule for restoration. */
internal object NativeMixRules {
    private val system = Class.forName("android.media.AudioSystem")
    private val mixClass = Class.forName("android.media.audiopolicy.AudioMix")
    private val ruleClass = Class.forName("android.media.audiopolicy.AudioMixingRule")
    fun construct(address: String): Any {
        val builderClass = Class.forName("android.media.audiopolicy.AudioMix\$Builder")
        val builder = builderClass.getConstructor(ruleClass).newInstance(mediaGameExcept(emptySet()))
        builderClass.getMethod("setRouteFlags", Int::class.javaPrimitiveType).invoke(builder, 2)
        builderClass.getMethod("setDevice", Int::class.javaPrimitiveType, String::class.java).invoke(builder, 0x8000, address)
        builderClass.getMethod("setFormat", AudioFormat::class.java).invoke(builder, AudioFormat.Builder().setSampleRate(48000)
            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
        return builderClass.getMethod("build").invoke(builder)
    }
    fun inspect(): List<PolicyMixSnapshot> {
        val text = dump("media.audio_policy")
        check(text.contains("Audio Policy Mix:")) { "ROUTE_INSPECTION_UNAVAILABLE" }
        return PolicyMixSnapshot.parse(text)
    }
    fun dump(service: String): String {
        val child = ProcessBuilder("/system/bin/dumpsys", service).redirectErrorStream(true).start()
        val text = StringBuilder()
        val reader = Thread({ runCatching { child.inputStream.bufferedReader().use { stream ->
            val buffer = CharArray(4096)
            while (true) { val n = stream.read(buffer); if (n < 0) break
                synchronized(text) { if (text.length < 512 * 1024) text.append(buffer, 0, minOf(n, 512 * 1024 - text.length)) }
            }
        } } }).apply { isDaemon = true; start() }
        try {
            check(child.waitFor(2, TimeUnit.SECONDS)) { "ROUTE_INSPECTION_TIMEOUT" }
            check(child.exitValue() == 0) { "ROUTE_INSPECTION_FAILED" }
            reader.join(500)
            check(!reader.isAlive) { "ROUTE_INSPECTION_INCOMPLETE" }
            synchronized(text) { check(text.length < 512 * 1024) { "ROUTE_INSPECTION_TRUNCATED" }; return text.toString() }
        } finally { child.destroy() }
    }
    fun update(mix: Any, rule: Any) {
        val mixes = java.lang.reflect.Array.newInstance(mixClass, 1)
        val rules = java.lang.reflect.Array.newInstance(ruleClass, 1)
        java.lang.reflect.Array.set(mixes, 0, mix); java.lang.reflect.Array.set(rules, 0, rule)
        check(system.getMethod("updatePolicyMixes", mixes.javaClass, rules.javaClass).invoke(null, mixes, rules) == 0) { "MIX_UPDATE_DENIED" }
    }
    fun mediaGameExcept(uids: Set<Int>): Any {
        val builderClass = Class.forName("android.media.audiopolicy.AudioMixingRule\$Builder")
        val builder = builderClass.getConstructor().newInstance()
        for (usage in listOf(AudioAttributes.USAGE_MEDIA, AudioAttributes.USAGE_GAME))
            builderClass.getMethod("addRule", AudioAttributes::class.java, Int::class.javaPrimitiveType)
                .invoke(builder, AudioAttributes.Builder().setUsage(usage).build(), 1)
        for (uid in uids) builderClass.getMethod("excludeMixRule", Int::class.javaPrimitiveType, Any::class.java)
            .invoke(builder, 4, uid)
        return builderClass.getMethod("build").invoke(builder)
    }
}
