package com.fyodor.tdspeed.shell

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.os.Looper
import android.os.Process
import android.os.SystemClock
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.abs
import kotlin.math.max
import kotlin.system.exitProcess

/** Shell-only synthetic routing test. No native DSP, microphone, audio files or real car involved. */
@SuppressLint("MissingPermission") // Authorized shell CAPTURE_AUDIO_OUTPUT path, not app microphone permission.
object PolicyChainSelfTest {
    private const val RATE = 48_000
    private const val SAMPLES = 4096

    @JvmStatic fun main(args: Array<String>) {
        val receipt = JSONObject().put("scope", "Synthetic policy competition and gain forwarding; not Android Auto qualification")
            .put("device", android.os.Build.FINGERPRINT)
        val cases = JSONArray()
        var passed = true
        try {
            check(Process.myUid() == 2000) { "Run with app_process as ADB shell UID 2000" }
            require(args.size == 2) { "Arguments: installed.package.name sourceUid" }
            val sourceUid = args[1].toInt()
            if (Looper.getMainLooper() == null) Looper.prepareMainLooper()
            val activityThread = Class.forName("android.app.ActivityThread")
            val thread = activityThread.getMethod("systemMain").invoke(null)
            val system = activityThread.getMethod("getSystemContext").invoke(thread) as Context
            val context = system.createPackageContext(args[0], Context.CONTEXT_INCLUDE_CODE or Context.CONTEXT_IGNORE_SECURITY)
            ShellAttribution.install(context)
            val ownerUid = context.applicationInfo.uid
            require(sourceUid != ownerUid && sourceUid != Process.myUid()) { "Separate source-app UID required" }
            receipt.put("helperUid", Process.myUid()).put("ownerUid", ownerUid).put("sourceUid", sourceUid)
            val specifications = listOf(
                Triple("car_first_none", true, AudioAttributes.ALLOW_CAPTURE_BY_NONE),
                Triple("processor_first_none", false, AudioAttributes.ALLOW_CAPTURE_BY_NONE),
                Triple("processor_first_all", false, AudioAttributes.ALLOW_CAPTURE_BY_ALL),
            )
            for ((label, carFirst, capture) in specifications) {
                val result = runCase(context, ownerUid, sourceUid, label, carFirst, capture, secondary = false)
                cases.put(result); if (result.getString("result") != "passed") passed = false
            }
            val prearmed = runCase(context, ownerUid, sourceUid, "processor_playing_before_car_all",
                false, AudioAttributes.ALLOW_CAPTURE_BY_ALL, secondary = false, delayedCar = true)
            cases.put(prearmed); if (prearmed.getString("result") != "passed") passed = false
            for (capture in listOf(AudioAttributes.ALLOW_CAPTURE_BY_NONE, AudioAttributes.ALLOW_CAPTURE_BY_ALL)) {
                val label = "secondary_" + if (capture == AudioAttributes.ALLOW_CAPTURE_BY_NONE) "none" else "all"
                val result = runCase(context, ownerUid, sourceUid, label, false, capture, secondary = true)
                cases.put(result); if (result.getString("result") != "passed") passed = false
            }
            val report = RouteReport.collect(context, android.os.Bundle())
            receipt.put("routeReportBinderBytes", report.getInt("binderBytes"))
            for (service in listOf("media.audio_policy", "media.audio_flinger", "audio")) {
                val text = report.getString(service).orEmpty()
                check(text.isNotEmpty() && !text.startsWith("[unavailable")) { "Route report unavailable: $service" }
                receipt.put("reportChars:$service", text.length)
            }
        } catch (error: Throwable) {
            passed = false; receipt.put("error", describe(error))
        }
        receipt.put("cases", cases).put("result", if (passed) "passed" else "failed")
        println("POLICY_CHAIN_RESULT=" + receipt.toString())
        exitProcess(if (passed) 0 else 1)
    }

    private fun runCase(context: Context, ownerUid: Int, sourceUid: Int, label: String,
        carFirst: Boolean, capture: Int, secondary: Boolean, delayedCar: Boolean = false): JSONObject {
        val result = JSONObject().put("case", label).put("outputCapturePolicy", capture)
            .put("carFlags", if (secondary) 3 else 2).put("carRegisteredFirst", carFirst)
            .put("carJoinsPlayingProcessor", delayedCar)
        val policies = mutableListOf<TestPolicy>()
        var output: AudioTrack? = null
        var inputPeak = 0f; var carPeak = 0f; var inputSamples = 0L; var carSamples = 0L; var written = 0L
        try {
            fun own() = TestPolicy(context, listOf(), listOf(Process.myUid(), ownerUid), false).also { policies.add(it); it.open() }
            // A car projection policy accepts both the source and processed output. The secondary
            // output-only probe matches shell UID alone so source tones cannot mask capture denial.
            fun car() = TestPolicy(context, if (secondary) listOf(Process.myUid()) else listOf(sourceUid, Process.myUid()),
                listOf(), secondary).also { policies.add(it); it.open() }
            val processor: TestPolicy
            var projection: TestPolicy? = null
            if (carFirst) { projection = car(); processor = own() }
            else { processor = own(); if (!delayedCar) projection = car() }
            result.put("processorAddress", processor.address)
            projection?.let { result.put("carAddress", it.address); it.record!!.startRecording() }
            processor.record!!.startRecording()
            if (!carFirst) {
                output = AudioTrack.Builder().setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .setAllowedCapturePolicy(capture).build())
                    .setAudioFormat(format(AudioFormat.CHANNEL_OUT_STEREO))
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(max(32_768, AudioTrack.getMinBufferSize(RATE, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT) * 2))
                    .build().also { check(it.state == AudioTrack.STATE_INITIALIZED); it.play() }
            }
            val input = FloatArray(SAMPLES); val carBuffer = FloatArray(SAMPLES)
            var pending = 0; var offset = 0
            var inputPeakBeforeCar = 0f
            val started = SystemClock.elapsedRealtime()
            val finish = started + if (delayedCar) 2500 else 2000
            while (SystemClock.elapsedRealtime() < finish) {
                val elapsed = SystemClock.elapsedRealtime() - started
                if (delayedCar && projection == null && elapsed >= 400) {
                    check(written > RATE / 4 && abs(inputPeakBeforeCar - .08f) < .005f) { "Processor was not forwarding source before car joined" }
                    result.put("forwardedSamplesBeforeCar", written).put("inputPeakBeforeCar", inputPeakBeforeCar.toDouble())
                        .put("carJoinedAtMs", elapsed)
                    projection = car().also { it.record!!.startRecording(); result.put("carAddress", it.address) }
                }
                val settled = elapsed >= if (delayedCar) 800 else 400
                val carRead = projection?.record?.read(carBuffer, 0, carBuffer.size, AudioRecord.READ_NON_BLOCKING) ?: 0
                check(carRead >= 0) { "Car read failed: $carRead" }
                if (settled) { carSamples += carRead; for (i in 0 until carRead) carPeak = max(carPeak, abs(carBuffer[i])) }
                if (pending == 0) {
                    val count = processor.record!!.read(input, 0, input.size, AudioRecord.READ_NON_BLOCKING)
                    check(count >= 0) { "Processor read failed: $count" }
                    if (delayedCar && projection == null) for (i in 0 until count) inputPeakBeforeCar = max(inputPeakBeforeCar, abs(input[i]))
                    if (settled) { inputSamples += count; for (i in 0 until count) inputPeak = max(inputPeak, abs(input[i])) }
                    if (output != null) { for (i in 0 until count) input[i] *= .5f; pending = count; offset = 0 }
                }
                if (pending > 0) {
                    val count = output!!.write(input, offset, pending, AudioTrack.WRITE_NON_BLOCKING)
                    check(count >= 0) { "Output write failed: $count" }
                    pending -= count; offset += count; written += count
                }
                Thread.sleep(1)
            }
            check(projection != null && inputSamples > RATE && carSamples > RATE) { "Insufficient captured samples" }
            if (carFirst) {
                check(inputPeak < .0005f) { "Processor unexpectedly received source: $inputPeak" }
                check(abs(carPeak - .08f) < .005f) { "Car did not receive original tone: $carPeak" }
            } else {
                check(abs(inputPeak - .08f) < .005f) { "Processor did not receive source: $inputPeak" }
                check(written > RATE) { "No processed audio forwarded" }
                val expected = if (secondary && capture == AudioAttributes.ALLOW_CAPTURE_BY_NONE) 0f else .04f
                check(abs(carPeak - expected) < .005f) { "Car peak $carPeak, expected $expected" }
            }
            result.put("result", "passed")
        } catch (error: Throwable) {
            result.put("result", "failed").put("error", describe(error))
        } finally {
            val errors = mutableListOf<String>()
            runCatching { output?.pause() }
            runCatching { output?.flush() }
            runCatching { output?.release() }.exceptionOrNull()?.let { errors.add(describe(it)) }
            for (policy in policies.asReversed()) runCatching { policy.close() }.exceptionOrNull()?.let { errors.add(describe(it)) }
            result.put("inputPeak", inputPeak.toDouble()).put("carPeak", carPeak.toDouble())
                .put("inputSamples", inputSamples).put("carSamples", carSamples).put("forwardedSamples", written)
                .put("policiesUnregistered", policies.all { it.unregistered }).put("cleanupErrors", JSONArray(errors))
            if (errors.isNotEmpty() || policies.any { !it.unregistered }) result.put("result", "failed")
            Thread.sleep(250)
        }
        return result
    }

    private class TestPolicy(private val context: Context, private val included: List<Int>,
        private val excluded: List<Int>, private val secondary: Boolean) : AutoCloseable {
        private val manager = context.getSystemService(AudioManager::class.java)
        private val policyClass = Class.forName("android.media.audiopolicy.AudioPolicy")
        private var policy: Any? = null
        var record: AudioRecord? = null; private set
        var address = ""; private set
        var unregistered = false; private set
        fun open() {
            val ruleClass = Class.forName("android.media.audiopolicy.AudioMixingRule")
            val ruleBuilderClass = Class.forName("android.media.audiopolicy.AudioMixingRule\$Builder")
            val ruleBuilder = ruleBuilderClass.getConstructor().newInstance()
            ruleBuilderClass.getMethod("addRule", AudioAttributes::class.java, Int::class.javaPrimitiveType)
                .invoke(ruleBuilder, AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build(), ruleClass.getField("RULE_MATCH_ATTRIBUTE_USAGE").getInt(null))
            val uidRule = ruleClass.getField("RULE_MATCH_UID").getInt(null)
            for (uid in included) ruleBuilderClass.getMethod("addMixRule", Int::class.javaPrimitiveType, Any::class.java).invoke(ruleBuilder, uidRule, uid)
            for (uid in excluded) ruleBuilderClass.getMethod("excludeMixRule", Int::class.javaPrimitiveType, Any::class.java).invoke(ruleBuilder, uidRule, uid)
            val mixClass = Class.forName("android.media.audiopolicy.AudioMix")
            val mixBuilderClass = Class.forName("android.media.audiopolicy.AudioMix\$Builder")
            val mixBuilder = mixBuilderClass.getConstructor(ruleClass).newInstance(ruleBuilderClass.getMethod("build").invoke(ruleBuilder))
            mixBuilderClass.getMethod("setFormat", AudioFormat::class.java).invoke(mixBuilder, format(AudioFormat.CHANNEL_OUT_STEREO))
            val flags = mixClass.getField("ROUTE_FLAG_LOOP_BACK").getInt(null) or if (secondary) mixClass.getField("ROUTE_FLAG_RENDER").getInt(null) else 0
            mixBuilderClass.getMethod("setRouteFlags", Int::class.javaPrimitiveType).invoke(mixBuilder, flags)
            val mix = mixBuilderClass.getMethod("build").invoke(mixBuilder)
            val builderClass = Class.forName("android.media.audiopolicy.AudioPolicy\$Builder")
            val builder = builderClass.getConstructor(Context::class.java).newInstance(context)
            builderClass.getMethod("addMix", mixClass).invoke(builder, mix)
            val created = builderClass.getMethod("build").invoke(builder)
            check(manager.javaClass.getMethod("registerAudioPolicy", policyClass).invoke(manager, created) == 0) { "Policy registration failed" }
            policy = created
            address = mixClass.getMethod("getRegistration").invoke(mix) as String
            val attributes = AudioAttributes.Builder()
            attributes.javaClass.getMethod("setInternalCapturePreset", Int::class.javaPrimitiveType).invoke(attributes, 8)
            attributes.javaClass.getMethod("addTag", String::class.java).invoke(attributes, "addr=$address")
            attributes.javaClass.getMethod("addTag", String::class.java).invoke(attributes, "fixedVolume")
            val recordBuilder = AudioRecord.Builder().setAudioFormat(format(AudioFormat.CHANNEL_IN_STEREO))
                .setBufferSizeInBytes(max(32_768, AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT) * 2))
            recordBuilder.javaClass.getMethod("setAudioAttributes", AudioAttributes::class.java).invoke(recordBuilder, attributes.build())
            record = recordBuilder.build().also { check(it.state == AudioRecord.STATE_INITIALIZED) }
        }
        override fun close() {
            var problem: Throwable? = null
            runCatching { record?.stop() }; runCatching { record?.release() }.exceptionOrNull()?.let { problem = it }; record = null
            val registered = policy
            if (registered != null) {
                runCatching {
                    manager.javaClass.getMethod("unregisterAudioPolicy", policyClass).invoke(manager, registered)
                    check(policyClass.getMethod("getStatus").invoke(registered) == policyClass.getField("POLICY_STATUS_UNREGISTERED").getInt(null))
                    unregistered = true
                }.exceptionOrNull()?.let { problem = problem ?: it }
            } else unregistered = true
            policy = null
            problem?.let { throw it }
        }
    }
    private fun format(channels: Int) = AudioFormat.Builder().setSampleRate(RATE)
        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setChannelMask(channels).build()
    private fun describe(error: Throwable): String {
        var actual = error
        while (actual.cause != null && actual.cause !== actual) actual = actual.cause!!
        return "${actual.javaClass.simpleName}: ${actual.message}"
    }
}
