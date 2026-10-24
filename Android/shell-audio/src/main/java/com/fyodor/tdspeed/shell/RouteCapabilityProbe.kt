package com.fyodor.tdspeed.shell

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.os.Looper
import android.os.Process
import org.json.JSONObject
import kotlin.system.exitProcess

/** Tests an owned route for a nonexistent UID. Never captures or alters real app playback. */
internal object RouteCapabilityProbe {
    @JvmStatic fun main(args: Array<String>) {
        val result = JSONObject(); var exit = 0; var registered: Any? = null; var manager: AudioManager? = null
        val policyClass = Class.forName("android.media.audiopolicy.AudioPolicy")
        try {
            check(Process.myUid() == 2000); require(args.size == 1)
            if (Looper.getMainLooper() == null) Looper.prepareMainLooper()
            val activity = Class.forName("android.app.ActivityThread")
            val thread = activity.getMethod("systemMain").invoke(null)
            val system = activity.getMethod("getSystemContext").invoke(thread) as Context
            val context = system.createPackageContext(args[0], Context.CONTEXT_INCLUDE_CODE or Context.CONTEXT_IGNORE_SECURITY)
            ShellAttribution.install(context)
            check(context.packageManager.getPackagesForUid(999999).isNullOrEmpty()) { "PROBE_UID_IN_USE" }
            val ruleClass = Class.forName("android.media.audiopolicy.AudioMixingRule")
            val ruleBuilder = Class.forName("android.media.audiopolicy.AudioMixingRule\$Builder")
            fun rule(usage: Int): Any {
                val builder = ruleBuilder.getConstructor().newInstance()
                ruleBuilder.getMethod("addRule", AudioAttributes::class.java, Int::class.javaPrimitiveType)
                    .invoke(builder, AudioAttributes.Builder().setUsage(usage).build(), 1)
                ruleBuilder.getMethod("addMixRule", Int::class.javaPrimitiveType, Any::class.java).invoke(builder, 4, 999999)
                return ruleBuilder.getMethod("build").invoke(builder)
            }
            val mixClass = Class.forName("android.media.audiopolicy.AudioMix")
            val mixBuilderClass = Class.forName("android.media.audiopolicy.AudioMix\$Builder")
            val builder = mixBuilderClass.getConstructor(ruleClass).newInstance(rule(AudioAttributes.USAGE_UNKNOWN))
            mixBuilderClass.getMethod("setRouteFlags", Int::class.javaPrimitiveType).invoke(builder, 2)
            mixBuilderClass.getMethod("setFormat", AudioFormat::class.java).invoke(builder,
                AudioFormat.Builder().setSampleRate(48000).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
            val mix = mixBuilderClass.getMethod("build").invoke(builder)
            val policyBuilderClass = Class.forName("android.media.audiopolicy.AudioPolicy\$Builder")
            val policyBuilder = policyBuilderClass.getConstructor(Context::class.java).newInstance(context)
            policyBuilderClass.getMethod("addMix", mixClass).invoke(policyBuilder, mix)
            val policy = policyBuilderClass.getMethod("build").invoke(policyBuilder)
            manager = context.getSystemService(AudioManager::class.java)
            check(manager.javaClass.getMethod("registerAudioPolicy", policyClass).invoke(manager, policy) == 0)
            registered = policy
            val address = mixClass.getMethod("getRegistration").invoke(mix) as String
            NativeMixRules.update(NativeMixRules.construct(address), rule(AudioAttributes.USAGE_MEDIA))
            val snapshot = NativeMixRules.inspect().single { it.address == address }
            check(snapshot.rules.toSet() == setOf("RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_MEDIA", "RULE_MATCH_UID 999999")) { "Native rule inspection mismatch: ${snapshot.rules}" }
            val configurations = manager.activePlaybackConfigurations
            for (config in configurations) { config.javaClass.getMethod("getClientUid").invoke(config); config.javaClass.getMethod("isActive").invoke(config) }
            result.put("nativeUpdateAndInspection", "passed").put("playbackConfigurationAccess", "passed").put("result", "passed")
        } catch (error: Throwable) {
            var cause = error; while (cause.cause != null && cause.cause !== cause) cause = cause.cause!!
            exit = 1; result.put("result", "failed").put("error", "${cause.javaClass.simpleName}: ${cause.message}")
        } finally {
            registered?.let { policy -> runCatching {
                manager!!.javaClass.getMethod("unregisterAudioPolicy", policyClass).invoke(manager, policy)
                check(policyClass.getMethod("getStatus").invoke(policy) == policyClass.getField("POLICY_STATUS_UNREGISTERED").getInt(null))
                result.put("policyUnregistered", true)
            }.exceptionOrNull()?.let { exit = 1; result.put("result", "failed").put("cleanupError", it.toString()) } }
        }
        println("ROUTE_CAPABILITY_PROBE=$result"); exitProcess(exit)
    }
}
