package com.fyodor.tdspeed.nativebridge

import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import kotlin.math.abs
import kotlin.math.sin

@RunWith(AndroidJUnit4::class)
class VarispeedEngineTest {
    private fun buffer(frames: Int) = ByteBuffer.allocateDirect(frames * 8).order(ByteOrder.nativeOrder())

    @Test fun androidBuildMatchesActualWindowsReferenceFixtures() {
        val context = InstrumentationRegistry.getInstrumentation().context
        val bytes = context.assets.open("p1-reference-fixtures.bin").use { it.readBytes() }
        val provenance = JSONObject(context.assets.open("p1-reference-provenance.json").bufferedReader().use { it.readText() })
        val digest = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
        assertEquals(provenance.getString("fixtureSha256"), digest)
        val fixture = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val magic = ByteArray(8); fixture.get(magic); assertEquals("TDSP1F01", String(magic, Charsets.US_ASCII))
        val cases = fixture.int; assertEquals(24, cases)
        val pcm = buffer(1024); val samples = pcm.asFloatBuffer()
        var maximumError = 0f; var compared = 0L; var maximumBacklogError = 0.0
        repeat(cases) { case ->
            val fs = fixture.int; val capacity = fixture.int; val kind = fixture.int
            val initial = fixture.float; val blocks = fixture.int
            VarispeedEngine(fs, capacity, initial).use { engine ->
                var offset = 0
                repeat(blocks) { block ->
                    val n = fixture.int; val semitones = fixture.float; val recenter = fixture.int != 0
                    assertTrue(n in 1..1024)
                    for (i in 0 until n) for (ch in 0..1) samples.put(2*i+ch, signal(kind, offset+i, ch, fs))
                    engine.process(pcm, n, semitones, recenter)
                    for (i in intArrayOf(0, n/2, n-1)) for (ch in 0..1) {
                        val expected = fixture.float; val actual = samples.get(2*i+ch)
                        assertTrue("nonfinite case=$case block=$block", actual.isFinite())
                        val error = abs(actual-expected); maximumError = maxOf(maximumError, error); compared++
                        assertTrue("sample case=$case block=$block error=$error", error <= 1e-6f)
                    }
                    val expectedBacklog = fixture.float; val expectedRecovery = fixture.int; val expectedPause = fixture.float
                    val state = engine.snapshot()
                    val backlogError = abs(state[VarispeedEngine.BACKLOG_SECONDS] - expectedBacklog)
                    maximumBacklogError = maxOf(maximumBacklogError, backlogError)
                    assertEquals("backlog case=$case block=$block", expectedBacklog.toDouble(), state[VarispeedEngine.BACKLOG_SECONDS], 1e-6)
                    assertEquals(expectedRecovery, state[VarispeedEngine.RECOVERY].toInt())
                    assertEquals(expectedPause.toDouble(), state[VarispeedEngine.RECOVERY_REMAINING]/fs, 1e-6)
                    assertEquals((offset+n).toDouble(), state[VarispeedEngine.WRITTEN], 0.0)
                    offset += n
                }
            }
        }
        assertFalse("unconsumed fixture records", fixture.hasRemaining())
        val report = JSONObject().put("result", "passed").put("fixtureSha256", digest)
            .put("cases", cases).put("comparedSparseSamples", compared).put("maxAbsSampleError", maximumError)
            .put("maxBacklogSecondsError", maximumBacklogError).put("tolerance", 1e-6)
            .put("device", Build.FINGERPRINT).put("runtimeAbis", Build.SUPPORTED_ABIS.joinToString())
            .put("nativeIdentity", NativeBridge.identity())
            .put("scope", "JNI dry core against Windows reference checkpoints; not live routing or car qualification")
        File(context.filesDir, "p1-reference-result.json").writeText(report.toString(2))
        println(report.toString())
    }

    @Test fun lifecycleAndDirectBufferContract() {
        assertThrows(IllegalArgumentException::class.java) { VarispeedEngine(48000, 100) }
        assertThrows(IllegalArgumentException::class.java) { VarispeedEngine(48000, VarispeedEngine.MAX_HISTORY_FRAMES+1) }
        val engine = VarispeedEngine(48000, 96000)
        val pcm = buffer(16)
        assertThrows(IllegalArgumentException::class.java) { engine.process(ByteBuffer.allocate(128), 16, 0f) }
        assertThrows(IllegalArgumentException::class.java) { engine.process(pcm.asReadOnlyBuffer(), 16, 0f) }
        assertThrows(IllegalArgumentException::class.java) { engine.process(pcm.duplicate().order(ByteOrder.BIG_ENDIAN), 16, 0f) }
        assertThrows(IllegalArgumentException::class.java) { engine.process(pcm, 17, 0f) }
        val unaligned = ByteBuffer.allocateDirect(129).apply { position(1) }.slice().order(ByteOrder.nativeOrder())
        assertThrows(IllegalArgumentException::class.java) { engine.process(unaligned, 16, 0f) }
        engine.process(pcm, 0, 0f, true)
        assertEquals(0.0, engine.snapshot()[VarispeedEngine.WRITTEN], 0.0)
        engine.close(); engine.close()
        assertThrows(IllegalStateException::class.java) { engine.process(pcm, 16, 0f) }
        assertThrows(IllegalStateException::class.java) { engine.snapshot() }
    }

    @Test fun invalidPcmDoesNotPoisonNativeHistory() {
        val pcm = buffer(257); val samples = pcm.asFloatBuffer()
        VarispeedEngine(48000, 96000).use { engine ->
            repeat(500) {
                for (i in 0 until 514) samples.put(i, if (i%2 == 0) Float.NaN else Float.POSITIVE_INFINITY)
                engine.process(pcm, 257, Float.NaN)
                for (i in 0 until 514) assertEquals(0f, samples.get(i), 0f)
            }
        }
    }

    @Test fun preallocatedStateMatchesSnapshotAndRejectsInvalidStorage() {
        VarispeedEngine(48000, 96000, -4f).use { engine ->
            val pcm = buffer(257)
            val state = ByteBuffer.allocateDirect(64).order(ByteOrder.nativeOrder())
            engine.process(pcm, 257, -4f)
            engine.readStateInto(state)
            val expected = engine.snapshot()
            for (i in expected.indices) assertEquals(expected[i], state.getDouble(i*8), 0.0)
            assertThrows(IllegalArgumentException::class.java) { engine.readStateInto(ByteBuffer.allocate(64).order(ByteOrder.nativeOrder())) }
            assertThrows(IllegalArgumentException::class.java) { engine.readStateInto(ByteBuffer.allocateDirect(63).order(ByteOrder.nativeOrder())) }
            assertThrows(IllegalArgumentException::class.java) { engine.readStateInto(state.asReadOnlyBuffer().order(ByteOrder.nativeOrder())) }
            val unaligned = ByteBuffer.allocateDirect(65).apply { position(1) }.slice().order(ByteOrder.nativeOrder())
            assertThrows(IllegalArgumentException::class.java) { engine.readStateInto(unaligned) }
        }
    }

    @Test fun fullOneGiBHistoryAllocatesAndReleasesNativeMemory() {
        assertEquals(1_073_741_824, VarispeedEngine.HISTORY_BYTES)
        val before = android.os.Debug.getNativeHeapAllocatedSize()
        val engine = VarispeedEngine(48000, VarispeedEngine.MAX_HISTORY_FRAMES)
        val allocated = android.os.Debug.getNativeHeapAllocatedSize()
        assertTrue("Full stereo history was not allocated", allocated - before >= VarispeedEngine.HISTORY_BYTES.toLong())
        val pcm = buffer(257)
        engine.process(pcm, 257, -4f)
        val state = engine.snapshot()
        assertEquals(134_217_728.0, state[VarispeedEngine.CAPACITY], 0.0)
        assertEquals(257.0, state[VarispeedEngine.WRITTEN], 0.0)
        engine.close()
        val released = android.os.Debug.getNativeHeapAllocatedSize()
        assertTrue("The 1 GiB native history remained allocated after close", released < before + 64L * 1024 * 1024)
        val report = JSONObject().put("result", "passed").put("historyBytes", VarispeedEngine.HISTORY_BYTES)
            .put("beforeNativeBytes", before).put("allocatedNativeBytes", allocated).put("afterCloseNativeBytes", released)
            .put("capacityFrames", state[VarispeedEngine.CAPACITY]).put("device", Build.FINGERPRINT)
        File(InstrumentationRegistry.getInstrumentation().context.filesDir, "one-gib-allocation.json").writeText(report.toString(2))
        println(report.toString())
    }

    private fun signal(kind: Int, frame: Int, channel: Int, fs: Int): Float {
        if (kind == 0) return (.2 * sin(2 * Math.PI * (if (channel == 0) 440 else 659) * frame / fs)).toFloat()
        if (kind == 1) return (if (channel == 0) 1f else -.5f) * (frame%100003).toFloat()/200006f
        if (kind == 3) return if (frame%997 == channel*31) .25f else 0f
        var x = frame*1664525 + 1013904223 + channel*374761393
        x = x xor (x ushr 13); x *= 1274126177; x = x xor (x ushr 16)
        return ((x and 65535).toFloat()/32768f - 1f)*.125f
    }
}
