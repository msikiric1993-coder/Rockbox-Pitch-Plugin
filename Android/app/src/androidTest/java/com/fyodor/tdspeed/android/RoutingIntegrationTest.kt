package com.fyodor.tdspeed.android

import android.content.ComponentName
import android.content.ServiceConnection
import android.os.Binder
import android.os.Bundle
import android.os.IBinder
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.fyodor.tdspeed.protocol.IAudioBridge
import com.fyodor.tdspeed.protocol.Protocol
import com.fyodor.tdspeed.shell.AudioUserService
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Requires an ADB-started, authorized Shizuku and the separate stereo-source app playing. */
@RunWith(AndroidJUnit4::class)
class RoutingIntegrationTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private lateinit var bridge: IAudioBridge
    private lateinit var connection: ServiceConnection
    private lateinit var args: Shizuku.UserServiceArgs
    private var session = 0L
    private var command = 0L

    @Before fun bind() {
        val received = CountDownLatch(1)
        val listener = Shizuku.OnBinderReceivedListener { received.countDown() }
        Shizuku.addBinderReceivedListenerSticky(listener)
        assertTrue("Start Shizuku through ADB before integration tests", received.await(10, TimeUnit.SECONDS))
        Shizuku.removeBinderReceivedListener(listener)
        assertEquals(2000, Shizuku.getUid())
        assertEquals(android.content.pm.PackageManager.PERMISSION_GRANTED, Shizuku.checkSelfPermission())
        val bound = CountDownLatch(1)
        connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName, service: IBinder) { bridge = IAudioBridge.Stub.asInterface(service); bound.countDown() }
            override fun onServiceDisconnected(name: ComponentName) {}
        }
        args = Shizuku.UserServiceArgs(ComponentName(context, AudioUserService::class.java))
            .daemon(false).processNameSuffix("audio_helper").debuggable(true).version(BuildConfig.VERSION_CODE)
        instrumentation.runOnMainSync { Shizuku.bindUserService(args, connection) }
        assertTrue("Shell helper did not bind", bound.await(10, TimeUnit.SECONDS))
        val probe = bridge.probe(Protocol.VERSION)
        assertTrue(probe.getBoolean("nativeLoaded")); assertTrue(probe.getBoolean("MODIFY_AUDIO_ROUTING"))
    }
    @After fun unbind() {
        if (::bridge.isInitialized) runCatching {
            if (bridge.snapshot().getLong("sessionId") != 0L) bridge.stop(session, ++command)
        }
        if (::args.isInitialized) instrumentation.runOnMainSync { Shizuku.unbindUserService(args, connection, true) }
    }
    private fun await(keepAlive: Boolean = true, condition: (Bundle) -> Boolean): Bundle {
        val deadline = SystemClock.elapsedRealtime() + 10_000
        var last = Bundle()
        while (SystemClock.elapsedRealtime() < deadline) {
            last = bridge.snapshot()
            if (condition(last)) return last
            if (last.getString("state") == "ERROR") fail(last.getString("error"))
            if (keepAlive && session != 0L && last.getLong("sessionId") == session) bridge.heartbeat(session)
            Thread.sleep(50)
        }
        throw AssertionError("Timed out: $last")
    }
    private fun begin() {
        session = SystemClock.elapsedRealtimeNanos(); command = 1
        bridge.start(session, command, Binder())
        await { it.getString("state") == "RUNNING" && it.getFloat("inputPeak") > 0.05f }
    }
    private fun shell(command: String): String = instrumentation.uiAutomation.executeShellCommand(command)
        .use { android.os.ParcelFileDescriptor.AutoCloseInputStream(it).bufferedReader().readText() }

    @Test fun livePitchKeepsHistoryAndRecenterIsOneShot() {
        begin()
        assertEquals(1024 * 1024 * 1024, bridge.snapshot().getInt("historyBytes"))
        val observations = JSONArray()
        fun capture(label: String, state: Bundle) {
            observations.put(JSONObject().put("label", label).put("frames", state.getLong("framesForwarded"))
                .put("pitch", state.getDouble("effectiveRate")).put("backlogSeconds", state.getDouble("backlogSeconds"))
                .put("appliedCommand", state.getLong("appliedListeningCommand")).put("recenterCount", state.getLong("recenterCount")))
        }
        var id = ++command
        bridge.setListening(session, id, -12f, false)
        var state = await { it.getLong("appliedListeningCommand") == id && kotlin.math.abs(it.getDouble("effectiveRate") - 0.5) < 0.0001 }
        capture("minus12", state)
        val startBacklog = state.getDouble("backlogSeconds"); val startFrames = state.getLong("framesForwarded")
        repeat(3) { Thread.sleep(1000); bridge.heartbeat(session) }
        state = bridge.snapshot()
        val elapsedSource = (state.getLong("framesForwarded") - startFrames) / 48000.0
        assertEquals(startBacklog + elapsedSource * .5, state.getDouble("backlogSeconds"), .03)
        assertTrue("Slowed live route did not retain history", state.getDouble("backlogSeconds") > 1.5)
        capture("history_accumulated", state)

        id = ++command; bridge.setListening(session, id, 0f, false)
        state = await { it.getLong("appliedListeningCommand") == id && kotlin.math.abs(it.getDouble("effectiveRate") - 1.0) < .0001 }
        val held = state.getDouble("backlogSeconds")
        Thread.sleep(1000); bridge.heartbeat(session); state = bridge.snapshot()
        assertEquals("Original pitch discarded accumulated delay", held, state.getDouble("backlogSeconds"), .01)
        capture("zero_pitch_retains_delay", state)

        id = ++command; bridge.setListening(session, id, 0f, true)
        state = await { it.getLong("appliedListeningCommand") == id }
        assertEquals(.25, state.getDouble("backlogSeconds"), .002)
        val resets = state.getLong("recenterCount")
        bridge.setListening(session, id, 0f, true) // Retry exact ID: no second reset.
        Thread.sleep(100); state = bridge.snapshot(); assertEquals(resets, state.getLong("recenterCount"))
        capture("recenter_one_shot", state)

        // Rapid pitch setters must not overwrite a pending recenter generation.
        bridge.setListening(session, ++command, -4f, true)
        id = ++command; bridge.setListening(session, id, -4f, false)
        state = await { it.getLong("appliedListeningCommand") == id }
        assertEquals(resets + 1, state.getLong("recenterCount"))
        capture("recenter_survives_next_pitch_command", state)
        id = ++command; bridge.setListening(session, id, 4f, false)
        state = await { it.getLong("appliedListeningCommand") == id && it.getDouble("effectiveRate") > 1.25 }
        capture("plus4", state)
        bridge.stop(session, ++command)
        await(keepAlive = false) { it.getString("state") == "STOPPED" && !it.getBoolean("policyRegistered") }
        session = 0
        val digest = java.security.MessageDigest.getInstance("SHA-256").digest(File(context.applicationInfo.sourceDir).readBytes())
            .joinToString("") { "%02x".format(it) }
        File(context.filesDir, "p2-live-pitch.json").writeText(JSONObject().put("result", "passed")
            .put("device", android.os.Build.FINGERPRINT).put("apkSha256", digest).put("observations", observations)
            .put("scope", "Live synthetic stereo source through RAM engine and speaker route; music/car listening separate").toString(2))
    }

    @Test fun replacementRouteDiagnosticsAndThirtyCleanupCycles() {
        val sourceLine = shell("cmd package list packages -U com.fyodor.tdspeed.testsource")
        val sourceUid = Regex("uid:(\\d+)").find(sourceLine)?.groupValues?.get(1)?.toInt()
            ?: error("Install and play the separate stereo-source app first")
        val cycles = JSONArray()
        for (cycle in 0 until 30) {
            begin()
            val active = bridge.snapshot()
            assertEquals("48000 Hz stereo float32", active.getString("format"))
            assertTrue(active.getBoolean("policyRegistered"))
            if (cycle == 0) {
                // Android may retain a terminating old track while its already-queued frames drain.
                // Judge the committed replacement route after that bounded transition.
                bridge.heartbeat(session); Thread.sleep(1000)
                val dump = shell("dumpsys media.audio_flinger")
                File(context.filesDir, "p0-active-audioflinger.txt").writeText(dump)
                assertTrue("Source should be on remote submix", activeUidOnDevice(dump, sourceUid, "AUDIO_DEVICE_OUT_REMOTE_SUBMIX"))
                assertFalse("Original source must not also render on speakers", activeUidOnDevice(dump, sourceUid, "AUDIO_DEVICE_OUT_SPEAKER"))
                assertTrue("Helper should render on speakers", activeUidOnDevice(dump, 2000, "AUDIO_DEVICE_OUT_SPEAKER"))
                assertFalse("Helper output must not feed capture", activeUidOnDevice(dump, 2000, "AUDIO_DEVICE_OUT_REMOTE_SUBMIX"))
                bridge.setDiagnostic(session, ++command, 0.5f, false)
                val half = await { it.getFloat("outputPeak") in 0.035f..0.045f }
                assertEquals(0.08f, half.getFloat("inputPeak"), 0.002f)
                bridge.setDiagnostic(session, ++command, 1f, true)
                await { it.getBoolean("swapChannels") && it.getFloat("outputPeak") > 0.07f }
            }
            bridge.stop(session, ++command)
            val stopped = await(keepAlive = false) { it.getString("state") == "STOPPED" }
            assertFalse(stopped.getBoolean("policyRegistered")); assertEquals(0L, stopped.getLong("sessionId"))
            cycles.put(JSONObject().put("cycle", cycle + 1).put("frames", stopped.getLong("framesForwarded")).put("state", stopped.getString("state")))
            session = 0
        }
        val restored = shell("dumpsys media.audio_flinger")
        assertTrue("Source should return to speakers", activeUidOnDevice(restored, sourceUid, "AUDIO_DEVICE_OUT_SPEAKER"))
        expiredControllerLeaseReleasesPolicy()
        val apkDigest = java.security.MessageDigest.getInstance("SHA-256")
            .digest(File(context.applicationInfo.sourceDir).readBytes()).joinToString("") { "%02x".format(it) }
        File(context.filesDir, "p0-routing-cycles.json").writeText(JSONObject().put("cycles", cycles).put("device", android.os.Build.FINGERPRINT)
            .put("apkSha256", apkDigest).put("controllerLeaseExpiryPassed", true)
            .put("result", "passed").put("scope", "Synthetic source on the recorded device speaker route; no commercial-source or car qualification").toString(2))
    }

    private fun expiredControllerLeaseReleasesPolicy() {
        begin()
        Thread.sleep(12_000) // Deliberately do not heartbeat. The helper watchdog must release the route.
        val snapshot = bridge.snapshot()
        assertEquals("ERROR", snapshot.getString("state"))
        assertTrue(snapshot.getString("error").orEmpty().contains("CONTROLLER_LEASE_EXPIRED"))
        assertFalse(snapshot.getBoolean("policyRegistered")); session = 0
    }

    private fun activeUidOnDevice(dump: String, uid: Int, device: String): Boolean {
        val active = Regex("(?m)^\\s*(?:[A-Z]\\s+)?\\d+\\s+yes\\s+\\d+/\\s*$uid\\b")
        return dump.split(Regex("(?m)^Output thread ")).drop(1).any { block ->
            val current = block.substringBefore("Local log:").substringBefore("\nInput thread ")
            val deviceLine = current.lineSequence().firstOrNull { it.trimStart().startsWith("Output devices:") }.orEmpty()
            deviceLine.contains(device) && active.containsMatchIn(current)
        }
    }
}
