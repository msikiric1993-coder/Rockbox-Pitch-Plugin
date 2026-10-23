package com.fyodor.tdspeed.shell

import android.content.Context
import android.app.ActivityManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.os.Bundle
import android.os.Process
import android.os.SystemClock
import com.fyodor.tdspeed.nativebridge.NativeBridge
import com.fyodor.tdspeed.nativebridge.VarispeedEngine
import com.fyodor.tdspeed.protocol.Protocol
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.roundToInt

/** P2 first slice: dry varispeed with bounded RAM history; car handling remains unqualified. */
internal class DiagnosticPipeline(private val context: Context, private val ownerUid: Int,
    private val allowCarCapture: Boolean = false, private val fatal: (Throwable) -> Unit) {
    private val active = AtomicBoolean(false)
    private val manager = context.getSystemService(AudioManager::class.java)
    private var policy: LoopbackPolicy? = null
    private var track: AudioTrack? = null
    private var worker: Thread? = null
    private var varispeed: VarispeedEngine? = null
    private data class Listening(val semitones: Float = 0f, val id: Long = 0, val recenterGeneration: Long = 0)
    @Volatile private var requested = Listening()
    @Volatile private var appliedPitch = 0f
    @Volatile private var appliedCommand = 0L
    @Volatile private var appliedRecenter = 0L
    @Volatile private var backlogSeconds = 0.25
    @Volatile private var effectiveRate = 1.0
    @Volatile private var recovery = 0
    @Volatile private var recoverySeconds = 0.0
    @Volatile private var recoveryCount = 0L
    @Volatile private var gain = 1f
    @Volatile private var swap = false
    @Volatile var progressAt = SystemClock.elapsedRealtime(); private set
    @Volatile private var frames = 0L
    @Volatile private var inputPeak = 0f
    @Volatile private var outputPeak = 0f
    @Volatile private var underruns = 0
    @Volatile private var encoding = AudioFormat.ENCODING_PCM_FLOAT
    @Volatile private var mode = manager.mode
    @Volatile private var route = "unknown"
    @Volatile private var outputAddress = ""
    @Volatile private var inputAddress = ""
    @Volatile private var outputDeviceId = -1
    @Volatile private var inputDeviceId = -1
    @Volatile private var reason = "PREPARING"
    private val modeListener = AudioManager.OnModeChangedListener { mode = it }
    private val modeExecutor = java.util.concurrent.Executors.newSingleThreadExecutor()
    val policyRegistered get() = policy != null

    fun start() {
        ShellAttribution.install(context)
        NativeBridge.identity() // Verify native loading before suppressing original playback.
        // Refuse before changing routing if a full buffer plus headroom is unavailable.
        // No silent fallback to a smaller history and no disk-backed audio file.
        val memory = ActivityManager.MemoryInfo()
        context.getSystemService(ActivityManager::class.java).getMemoryInfo(memory)
        check(memory.availMem >= VarispeedEngine.HISTORY_BYTES.toLong() + 256L * 1024 * 1024) {
            "INSUFFICIENT_MEMORY: close some apps, then retry the 1 GiB buffer"
        }
        // Allocate history before taking ownership of the source route.
        varispeed = VarispeedEngine(Protocol.SAMPLE_RATE, VarispeedEngine.MAX_HISTORY_FRAMES)
        manager.addOnModeChangedListener(modeExecutor, modeListener)
        val failures = mutableListOf<Throwable>()
        for (candidate in listOf(AudioFormat.ENCODING_PCM_FLOAT, AudioFormat.ENCODING_PCM_16BIT)) {
            try {
                val output = openTrack(candidate); track = output
                val inputPolicy = LoopbackPolicy(context, ownerUid, candidate, FocusObserver()); policy = inputPolicy
                val record = inputPolicy.open()
                encoding = candidate
                record.startRecording()
                active.set(true); progressAt = SystemClock.elapsedRealtime()
                worker = Thread({ transport(record, output, inputPolicy) }, "TDSpeed-stereo-io").also { it.start() }
                return
            } catch (error: Throwable) {
                failures.add(error)
                // Refuse a fallback if restoration of the first policy failed.
                closeAudio()
            }
        }
        runCatching { manager.removeOnModeChangedListener(modeListener) }; modeExecutor.shutdownNow()
        throw IllegalStateException("AUDIO_START_FAILED: float and PCM16 routes failed", failures.last())
    }

    fun diagnostic(newGain: Float, newSwap: Boolean) {
        require(newGain.isFinite() && newGain in 0f..1f)
        gain = newGain; swap = newSwap
    }
    fun listening(semitones: Float, recenter: Boolean, commandId: Long) {
        require(semitones.isFinite() && semitones in -12f..12f)
        val previous = requested
        requested = Listening(semitones, commandId, previous.recenterGeneration + if (recenter) 1 else 0)
    }
    private fun openTrack(formatEncoding: Int): AudioTrack {
        val minimum = AudioTrack.getMinBufferSize(Protocol.SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, formatEncoding)
        check(minimum > 0) { "FORMAT_UNSUPPORTED: AudioTrack $minimum" }
        return AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).setAllowedCapturePolicy(
                    if (allowCarCapture) AudioAttributes.ALLOW_CAPTURE_BY_ALL else AudioAttributes.ALLOW_CAPTURE_BY_NONE).build())
            .setAudioFormat(AudioFormat.Builder().setSampleRate(Protocol.SAMPLE_RATE).setEncoding(formatEncoding)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
            .setBufferSizeInBytes(maxOf(minimum * 2, Protocol.QUANTUM * 2 * LoopbackPolicy.bytesPerSample(formatEncoding) * 4))
            .setTransferMode(AudioTrack.MODE_STREAM).build().also {
                if (it.state != AudioTrack.STATE_INITIALIZED) { it.release(); error("OUTPUT_INITIALIZATION_FAILED") }
            }
    }

    private fun transport(record: AudioRecord, output: AudioTrack, routePolicy: LoopbackPolicy) {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        val samples = Protocol.QUANTUM * Protocol.CHANNELS
        // The engine owns the 250 ms starting offset. Do not stack the P0 queue on it.
        val wire = ByteBuffer.allocateDirect(samples * LoopbackPolicy.bytesPerSample(encoding)).order(ByteOrder.nativeOrder())
        val floats = ByteBuffer.allocateDirect(samples * 4).order(ByteOrder.nativeOrder())
        val floatView = floats.asFloatBuffer()
        val shortView = wire.asShortBuffer()
        val engineState = ByteBuffer.allocateDirect(64).order(ByteOrder.nativeOrder())
        val processor = checkNotNull(varispeed)
        var resetGeneration = 0L
        var routeRefreshAt = 0L
        var playing = false
        try {
            while (active.get()) {
                wire.clear()
                val read = record.read(wire, wire.capacity(), AudioRecord.READ_BLOCKING)
                if (!active.get()) break
                check(read > 0 && read % (2 * LoopbackPolicy.bytesPerSample(encoding)) == 0) { "CAPTURE_READ_FAILED: $read" }
                progressAt = SystemClock.elapsedRealtime()
                val bytes = read
                val count = bytes / LoopbackPolicy.bytesPerSample(encoding)
                val engine = if (encoding == AudioFormat.ENCODING_PCM_FLOAT) wire else floats
                if (encoding != AudioFormat.ENCODING_PCM_FLOAT) {
                    for (i in 0 until count) floatView.put(i, shortView.get(i) / 32768f)
                }
                val input = NativeBridge.process(engine, count, 1f, false)
                val controls = requested
                processor.process(engine, count / 2, controls.semitones, controls.recenterGeneration != resetGeneration)
                resetGeneration = controls.recenterGeneration
                processor.readStateInto(engineState)
                backlogSeconds = engineState.getDouble(16); effectiveRate = engineState.getDouble(24)
                recovery = engineState.getDouble(32).toInt()
                recoverySeconds = engineState.getDouble(40) / Protocol.SAMPLE_RATE
                recoveryCount = engineState.getDouble(48).toLong()
                appliedPitch = controls.semitones; appliedRecenter = resetGeneration; appliedCommand = controls.id
                val attenuation = if (mode == AudioManager.MODE_IN_CALL || mode == AudioManager.MODE_IN_COMMUNICATION) 0f else routePolicy.focus.attenuation
                val packed = NativeBridge.process(engine, count, gain * attenuation, swap)
                inputPeak = NativeBridge.inputPeak(input); outputPeak = NativeBridge.outputPeak(packed)
                if (encoding != AudioFormat.ENCODING_PCM_FLOAT) {
                    for (i in 0 until count) shortView.put(i, (floatView.get(i) * 32768f).roundToInt().coerceIn(-32768, 32767).toShort())
                }
                wire.position(0); wire.limit(bytes)
                if (!playing) { output.play(); playing = true }
                while (wire.hasRemaining() && active.get()) {
                    val n = output.write(wire, wire.remaining(), AudioTrack.WRITE_BLOCKING)
                    check(n > 0) { "OUTPUT_WRITE_FAILED: $n" }
                }
                // Read native AudioTrack state only on its owning transport thread.
                // Binder snapshots can race stop/release, so publish a cached value.
                underruns = output.underrunCount
                frames += count / 2; progressAt = SystemClock.elapsedRealtime()
                reason = if (attenuation == 0f) "INTERRUPTED_CANDIDATE" else if (recovery != 0) "BUFFER_RECOVERY"
                    else if (inputPeak > 0.00001f) "SIGNAL_PRESENT" else "SOURCE_IDLE_OR_UNVERIFIED"
                val now = SystemClock.elapsedRealtime()
                if (now >= routeRefreshAt) {
                    val sink = output.routedDevice
                    route = sink?.let { "${it.type}:${it.productName}" } ?: "unrouted"
                    outputAddress = sink?.address.orEmpty(); outputDeviceId = sink?.id ?: -1
                    val source = record.routedDevice
                    inputAddress = source?.address.orEmpty(); inputDeviceId = source?.id ?: -1
                    routeRefreshAt = now + 250
                }
            }
        } catch (error: Throwable) { if (active.get()) fatal(error) }
    }

    fun snapshot() = Bundle().apply {
        putBoolean("allowCarCapture", allowCarCapture)
        putString("outputCapturePolicy", if (allowCarCapture) "ALLOW_CAPTURE_BY_ALL" else "ALLOW_CAPTURE_BY_NONE")
        putString("captureMixAddress", policy?.registrationAddress.orEmpty())
        putString("outputDeviceAddress", outputAddress); putInt("outputDeviceId", outputDeviceId)
        putString("inputDeviceAddress", inputAddress); putInt("inputDeviceId", inputDeviceId)
        putLong("framesForwarded", frames); putFloat("inputPeak", inputPeak); putFloat("outputPeak", outputPeak)
        putFloat("diagnosticGain", gain); putBoolean("swapChannels", swap)
        putFloat("pitchSemitones", appliedPitch); putLong("appliedListeningCommand", appliedCommand)
        putLong("recenterCount", appliedRecenter); putDouble("backlogSeconds", backlogSeconds)
        putDouble("effectiveRate", effectiveRate); putInt("bufferRecovery", recovery)
        putDouble("recoverySeconds", recoverySeconds); putLong("recoveryCount", recoveryCount)
        putDouble("historyCapacitySeconds", VarispeedEngine.MAX_HISTORY_FRAMES.toDouble() / Protocol.SAMPLE_RATE)
        putInt("historyBytes", VarispeedEngine.HISTORY_BYTES)
        putString("format", "48000 Hz stereo ${if (encoding == AudioFormat.ENCODING_PCM_FLOAT) "float32" else "PCM16 boundary / float32 engine"}")
        putInt("underruns", underruns); putString("outputRoute", route)
        putInt("audioMode", mode); putString("signalStatus", reason)
        putBoolean("focusObserverRegistered", policy?.focusAttached == true)
        putLong("focusEventCount", policy?.focus?.eventCount ?: 0)
        putStringArrayList("focusTrace", policy?.focus?.traces() ?: arrayListOf())
    }

    fun stop() {
        active.set(false)
        runCatching { policy?.record?.stop() }; runCatching { track?.pause() }; runCatching { track?.flush() }; runCatching { track?.stop() }
        worker?.let { if (it !== Thread.currentThread()) it.join(1500) }
        val cleanup = runCatching { closeAudio() }.exceptionOrNull()
        runCatching { manager.removeOnModeChangedListener(modeListener) }; modeExecutor.shutdownNow()
        check(worker?.isAlive != true) { "CLEANUP_INCOMPLETE: transport did not stop" }
        worker = null
        varispeed?.close(); varispeed = null
        cleanup?.let { throw it }
    }
    private fun closeAudio() {
        var failure: Throwable? = null
        try { policy?.close() } catch (error: Throwable) { failure = error } finally { policy = null }
        try { track?.release() } catch (error: Throwable) { failure = failure ?: error } finally { track = null }
        failure?.let { throw it }
    }
}
