package com.fyodor.tdspeed.shell

import android.content.Context
import org.json.JSONObject
import java.io.PrintWriter
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** Separate shell process retains restoration responsibility if the audio helper dies. */
internal class CarRouteController(private val context: Context, private val ownerUid: Int,
    private val captureAddress: String = "") : AutoCloseable {
    private var child: java.lang.Process? = null
    private var writer: PrintWriter? = null
    private var statusReader: Thread? = null
    private val heartbeat = Executors.newSingleThreadScheduledExecutor()
    @Volatile var status = "STARTING"; private set
    @Volatile var detail = ""; private set
    @Volatile var excluded = ""; private set
    @Volatile var discovery = "none"; private set
    fun start(testAddress: String? = null, testSource: Int = -1, replayMode: String? = null) {
        val command = mutableListOf("/system/bin/app_process", "/system/bin", CarRouteGuardian::class.java.name,
            context.packageName, ownerUid.toString())
        if (testAddress != null) command.addAll(listOf(testAddress, testSource.toString()))
        if (replayMode != null) { require(testAddress != null); command.add(replayMode) }
        val process = ProcessBuilder(command).redirectErrorStream(true).apply {
            environment()["CLASSPATH"] = context.applicationInfo.sourceDir
        }.start()
        child = process
        writer = PrintWriter(process.outputStream, true)
        if (captureAddress.isNotEmpty()) writer!!.println("CAPTURE $captureAddress")
        statusReader = Thread({ process.inputStream.bufferedReader().useLines { lines -> lines.forEach { line ->
            if (line.startsWith("CAR_ROUTE_STATUS=")) runCatching {
                val state = JSONObject(line.substringAfter('='))
                status = state.getString("status"); detail = state.optString("detail"); excluded = state.optString("excluded")
                discovery = state.optString("discovery", "none")
            }
        } }; if (status != "RESTORED") status = "UNAVAILABLE" }, "Varispeed-car-status").apply { isDaemon = true; start() }
        heartbeat.scheduleAtFixedRate({ runCatching { writer?.println("PING") } }, 0, 1, TimeUnit.SECONDS)
    }
    fun awaitActive(timeoutMs: Long): Boolean {
        val until = android.os.SystemClock.elapsedRealtime() + timeoutMs
        while (android.os.SystemClock.elapsedRealtime() < until) {
            if (status == "REROUTED") return true
            if (status == "ERROR" || status == "UNAVAILABLE") return false
            Thread.sleep(25)
        }
        return false
    }
    fun closeAsLostParentForTest() { finish(false) }
    override fun close() { finish(true) }
    private fun finish(orderly: Boolean) {
        heartbeat.shutdownNow()
        if (orderly) writer?.println("STOP")
        writer?.close(); writer = null
        child?.let {
            check(it.waitFor(5, TimeUnit.SECONDS)) { "CAR_ROUTE_RESTORE_PENDING" }
            statusReader?.join(500)
            check(it.exitValue() == 0) { "CAR_ROUTE_RESTORE_FAILED: $detail" }
        }
        child = null
    }
}
