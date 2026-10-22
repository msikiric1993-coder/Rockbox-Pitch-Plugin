package com.fyodor.tdspeed.nativebridge

import java.nio.ByteBuffer

/** P0 transport diagnostics only. This is deliberately not the varispeed engine. */
object NativeBridge {
    init { System.loadLibrary("tdspeed_probe") }
    external fun identity(): String
    external fun process(buffer: ByteBuffer, sampleCount: Int, gain: Float, swap: Boolean): Long
    fun inputPeak(packed: Long) = Float.fromBits((packed ushr 32).toInt())
    fun outputPeak(packed: Long) = Float.fromBits(packed.toInt())
}
