package com.fyodor.tdspeed.nativebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** P1 dry engine. Prepare/close off the audio thread; one serialized processing owner. */
class VarispeedEngine(sampleRate: Int, historyFrames: Int, initialSemitones: Float = 0f) : AutoCloseable {
    private var handle: Long
    init {
        NativeBridge.identity()
        // User-selected simple RAM edition: one GiB total for both float channels.
        require(sampleRate in 8000..192000 && historyFrames >= sampleRate * 2 && historyFrames <= MAX_HISTORY_FRAMES)
        handle = create(sampleRate, historyFrames, initialSemitones)
        check(handle != 0L)
    }

    /** Processes from byte offset zero; buffer position/limit do not select audio. No output allocation. */
    @Synchronized fun process(stereo: ByteBuffer, frames: Int, semitones: Float, recenter: Boolean = false) {
        check(handle != 0L) { "Engine is closed" }
        require(stereo.isDirect && !stereo.isReadOnly && stereo.order() == ByteOrder.nativeOrder())
        require(frames >= 0 && frames.toLong() * 8 <= stereo.capacity())
        processNative(handle, stereo, frames, semitones, recenter)
    }

    /** Control-thread diagnostics only. Allocates an array; never call from the audio callback. */
    @Synchronized fun snapshot(): DoubleArray {
        check(handle != 0L) { "Engine is closed" }
        return snapshotNative(handle)
    }

    /** Preallocated native-order eight-double state block for the processing owner. */
    @Synchronized fun readStateInto(state: ByteBuffer) {
        check(handle != 0L) { "Engine is closed" }
        require(state.isDirect && !state.isReadOnly && state.order() == ByteOrder.nativeOrder() && state.capacity() >= 64)
        stateIntoNative(handle, state)
    }

    @Synchronized override fun close() {
        if (handle != 0L) { destroy(handle); handle = 0L }
    }

    private external fun create(sampleRate: Int, historyFrames: Int, semitones: Float): Long
    private external fun processNative(handle: Long, stereo: ByteBuffer, frames: Int, semitones: Float, recenter: Boolean)
    private external fun snapshotNative(handle: Long): DoubleArray
    private external fun stateIntoNative(handle: Long, state: ByteBuffer)
    private external fun destroy(handle: Long)

    companion object {
        const val HISTORY_BYTES = 1024 * 1024 * 1024
        const val MAX_HISTORY_FRAMES = HISTORY_BYTES / 8
        // snapshot indices: written, read position, backlog seconds, rate, recovery,
        // recovery frames remaining, recovery count, capacity frames.
        const val WRITTEN = 0
        const val READ_POSITION = 1
        const val BACKLOG_SECONDS = 2
        const val EFFECTIVE_RATE = 3
        const val RECOVERY = 4
        const val RECOVERY_REMAINING = 5
        const val RECOVERY_COUNT = 6
        const val CAPACITY = 7
    }
}
