package com.fyodor.tdspeed.shell

import android.annotation.SuppressLint

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.os.Process
import com.fyodor.tdspeed.protocol.Protocol

/** Hidden APIs are restricted to the shell process. No microphone or MediaProjection fallback. */
internal class LoopbackPolicy(
    private val context: Context,
    private val ownerUid: Int,
    private val encoding: Int,
    val focus: FocusObserver,
) : AutoCloseable {
    private val manager = context.getSystemService(AudioManager::class.java)
    private var policy: Any? = null
    var record: AudioRecord? = null; private set
    var focusAttached = false; private set
    var registrationAddress = ""; private set
    private val policyClass = Class.forName("android.media.audiopolicy.AudioPolicy")

    fun open(): AudioRecord {
        check(policy == null)
        try {
            val ruleClass = Class.forName("android.media.audiopolicy.AudioMixingRule")
            val ruleBuilderClass = Class.forName("android.media.audiopolicy.AudioMixingRule\$Builder")
            val builder = ruleBuilderClass.getConstructor().newInstance()
            val usageRule = ruleClass.getField("RULE_MATCH_ATTRIBUTE_USAGE").getInt(null)
            val uidRule = ruleClass.getField("RULE_MATCH_UID").getInt(null)
            val addRule = ruleBuilderClass.getMethod("addRule", AudioAttributes::class.java, Int::class.javaPrimitiveType)
            for (usage in listOf(AudioAttributes.USAGE_MEDIA, AudioAttributes.USAGE_UNKNOWN)) {
                addRule.invoke(builder, AudioAttributes.Builder().setUsage(usage).build(), usageRule)
            }
            val excludeUid = ruleBuilderClass.getMethod("excludeMixRule", Int::class.javaPrimitiveType, Any::class.java)
            excludeUid.invoke(builder, uidRule, Process.myUid())
            if (ownerUid != Process.myUid()) excludeUid.invoke(builder, uidRule, ownerUid)
            val rule = ruleBuilderClass.getMethod("build").invoke(builder)
            val mixClass = Class.forName("android.media.audiopolicy.AudioMix")
            val mixBuilderClass = Class.forName("android.media.audiopolicy.AudioMix\$Builder")
            val mixBuilder = mixBuilderClass.getConstructor(ruleClass).newInstance(rule)
            val format = AudioFormat.Builder().setSampleRate(Protocol.SAMPLE_RATE).setEncoding(encoding)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build()
            mixBuilderClass.getMethod("setFormat", AudioFormat::class.java).invoke(mixBuilder, format)
            mixBuilderClass.getMethod("setRouteFlags", Int::class.javaPrimitiveType)
                .invoke(mixBuilder, mixClass.getField("ROUTE_FLAG_LOOP_BACK").getInt(null))
            val mix = mixBuilderClass.getMethod("build").invoke(mixBuilder)
            val policyBuilderClass = Class.forName("android.media.audiopolicy.AudioPolicy\$Builder")
            val policyBuilder = policyBuilderClass.getConstructor(Context::class.java).newInstance(context)
            policyBuilderClass.getMethod("addMix", mixClass).invoke(policyBuilder, mix)
            // This listener class is supplied by Android, never by the compile-only stub module.
            val listenerClass = Class.forName("android.media.audiopolicy.AudioPolicy\$AudioPolicyFocusListener")
            policyBuilderClass.getMethod("setAudioPolicyFocusListener", listenerClass).invoke(policyBuilder, focus)
            policyBuilderClass.getMethod("setIsAudioFocusPolicy", Boolean::class.javaPrimitiveType).invoke(policyBuilder, false)
            val created = policyBuilderClass.getMethod("build").invoke(policyBuilder)
            val result = manager.javaClass.getMethod("registerAudioPolicy", policyClass).invoke(manager, created) as Int
            check(result == 0) { "POLICY_REGISTRATION_FAILED: $result" }
            policy = created // Own registration before any subsequent failure can occur.
            focusAttached = true
            val address = mixClass.getMethod("getRegistration").invoke(mix) as String
            registrationAddress = address
            val attributesBuilder = AudioAttributes.Builder()
            attributesBuilder.javaClass.getMethod("setInternalCapturePreset", Int::class.javaPrimitiveType).invoke(attributesBuilder, 8)
            attributesBuilder.javaClass.getMethod("addTag", String::class.java).invoke(attributesBuilder, "addr=$address")
            attributesBuilder.javaClass.getMethod("addTag", String::class.java).invoke(attributesBuilder, "fixedVolume")
            val inputFormat = AudioFormat.Builder().setSampleRate(Protocol.SAMPLE_RATE).setEncoding(encoding)
                .setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build()
            val min = AudioRecord.getMinBufferSize(Protocol.SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO, encoding)
            check(min > 0) { "FORMAT_UNSUPPORTED: AudioRecord $min" }
            val recordBuilder = AudioRecord.Builder().setAudioFormat(inputFormat)
                .setBufferSizeInBytes(maxOf(min * 2, Protocol.QUANTUM * 2 * bytesPerSample(encoding) * 4))
            recordBuilder.javaClass.getMethod("setAudioAttributes", AudioAttributes::class.java)
                .invoke(recordBuilder, attributesBuilder.build())
            // This hidden REMOTE_SUBMIX path runs in the authorized shell helper
            // with CAPTURE_AUDIO_OUTPUT, not a normal app microphone recorder.
            // Any SecurityException still follows the cleanup/rethrow path below.
            @SuppressLint("MissingPermission")
            val opened = recordBuilder.build()
            record = opened
            check(opened.state == AudioRecord.STATE_INITIALIZED) { "CAPTURE_INITIALIZATION_FAILED" }
            return opened
        } catch (error: Throwable) {
            val cleanup = runCatching { close() }.exceptionOrNull()
            cleanup?.let { error.addSuppressed(it) }
            throw error
        }
    }

    override fun close() {
        val errors = mutableListOf<Throwable>()
        record?.let { runCatching { it.release() }.exceptionOrNull()?.let(errors::add) }; record = null
        policy?.let { registered ->
            runCatching { manager.javaClass.getMethod("unregisterAudioPolicy", policyClass).invoke(manager, registered) }
                .exceptionOrNull()?.let(errors::add)
        }
        policy = null
        registrationAddress = ""
        if (errors.isNotEmpty()) throw IllegalStateException("CLEANUP_INCOMPLETE: audio policy", errors.first())
    }
    companion object { fun bytesPerSample(encoding: Int) = if (encoding == AudioFormat.ENCODING_PCM_FLOAT) 4 else 2 }
}
