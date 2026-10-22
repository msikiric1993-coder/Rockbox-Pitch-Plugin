package com.fyodor.tdspeed.nativebridge

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer
import java.nio.ByteOrder

@RunWith(AndroidJUnit4::class)
class NativeBridgeTest {
    private fun buffer() = ByteBuffer.allocateDirect(16).order(ByteOrder.nativeOrder())
    @Test fun unityIsExactAndStereoSwapIsIndependent() {
        val b = buffer(); val f = b.asFloatBuffer()
        f.put(floatArrayOf(0.25f, -0.5f, 0.125f, 0.75f))
        val packed = NativeBridge.process(b, 4, 1f, false)
        assertEquals(0.25f, f.get(0), 0f); assertEquals(-0.5f, f.get(1), 0f)
        assertEquals(0.75f, NativeBridge.inputPeak(packed), 0f)
        NativeBridge.process(b, 4, 0.5f, true)
        assertEquals(-0.25f, f.get(0), 0f); assertEquals(0.125f, f.get(1), 0f)
        assertEquals(0.375f, f.get(2), 0f); assertEquals(0.0625f, f.get(3), 0f)
    }
    @Test fun rejectsInvalidAndNonDirectBuffers() {
        assertThrows(IllegalArgumentException::class.java) { NativeBridge.process(ByteBuffer.allocate(16), 4, 1f, false) }
        assertThrows(IllegalArgumentException::class.java) { NativeBridge.process(buffer(), 3, 1f, false) }
        assertThrows(IllegalArgumentException::class.java) { NativeBridge.process(buffer(), 8, 1f, false) }
        assertThrows(IllegalArgumentException::class.java) { NativeBridge.process(buffer(), 4, Float.NaN, false) }
    }
    @Test fun nonFiniteInputCannotReachOutput() {
        val b = buffer(); val f = b.asFloatBuffer()
        f.put(floatArrayOf(Float.NaN, Float.POSITIVE_INFINITY, -0.25f, 0.5f))
        NativeBridge.process(b, 4, 1f, false)
        assertEquals(0f, f.get(0), 0f); assertEquals(0f, f.get(1), 0f)
        assertEquals(-0.25f, f.get(2), 0f)
    }
}
