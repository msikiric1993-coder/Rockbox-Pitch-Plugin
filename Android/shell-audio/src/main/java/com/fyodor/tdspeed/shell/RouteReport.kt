package com.fyodor.tdspeed.shell

import android.content.Context
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/** Explicit user-requested diagnostics. Never records PCM or changes another policy. */
internal object RouteReport {
    @Synchronized fun collect(context: Context, snapshot: Bundle): Bundle = Bundle().apply {
        putAll(snapshot)
        putString("reportScope", "Routing evidence only; Android Auto playback is not qualified")
        putString("buildFingerprint", Build.FINGERPRINT)
        putLong("capturedAtEpochMs", System.currentTimeMillis())
        val manager = context.getSystemService(AudioManager::class.java)
        val devices = JSONArray()
        for (device in (manager.getDevices(AudioManager.GET_DEVICES_INPUTS).toList() +
            manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).toList()).distinctBy { it.id }.take(64)) {
            devices.put(JSONObject().put("id", device.id).put("type", device.type)
                .put("address", device.address.take(256)).put("name", device.productName.toString().take(128)).put("sink", device.isSink))
        }
        putString("devicesJson", devices.toString())
        // Raw dumps can include package names/device addresses. Shared only via the user's chooser.
        for (service in listOf("media.audio_policy", "media.audio_flinger", "audio")) {
            putString(service, dump(service))
        }
    }.also { report ->
        val parcel = android.os.Parcel.obtain()
        try {
            parcel.writeBundle(report)
            check(parcel.dataSize() < 512 * 1024) { "REPORT_TOO_LARGE" }
            report.putInt("binderBytes", parcel.dataSize())
        } finally { parcel.recycle() }
    }

    private fun dump(service: String): String {
        var process: java.lang.Process? = null
        return try {
            val child = ProcessBuilder("/system/bin/dumpsys", service).redirectErrorStream(true).start()
            process = child
            val text = StringBuilder()
            var truncated = false
            val reader = Thread({
                runCatching {
                    child.inputStream.bufferedReader().use { stream ->
                        val buffer = CharArray(4096)
                        while (true) {
                            val count = stream.read(buffer)
                            if (count < 0) break
                            synchronized(text) {
                                // Binder strings use UTF-16: keep all three dumps safely below 1 MiB.
                                val remaining = 48 * 1024 - text.length
                                if (remaining > 0) text.append(buffer, 0, minOf(count, remaining))
                                if (count > remaining) truncated = true
                            }
                        }
                    }
                }
            }, "Varispeed-report-reader").apply { isDaemon = true; start() }
            val done = child.waitFor(3, TimeUnit.SECONDS)
            if (!done) child.destroyForcibly()
            reader.join(1000)
            synchronized(text) {
                text.toString() + (if (!done) "\n[timeout]" else "") + (if (truncated) "\n[truncated]" else "")
            }
        } catch (error: Exception) { "[unavailable: ${error.javaClass.simpleName}: ${error.message}]" }
        finally { process?.destroy() }
    }
}
