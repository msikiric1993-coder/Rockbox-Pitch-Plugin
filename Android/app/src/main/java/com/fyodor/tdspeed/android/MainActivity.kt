package com.fyodor.tdspeed.android

import android.Manifest
import android.content.ComponentName
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.IBinder
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.*
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.core.content.FileProvider
import java.io.File
import rikka.shizuku.Shizuku

class MainActivity : ComponentActivity() {
    private var service by mutableStateOf<BridgeService?>(null)
    private var permissionAction: (() -> Unit)? = null
    private val notificationPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { }
    private val permissionListener = Shizuku.OnRequestPermissionResultListener { _, result ->
        runOnUiThread {
            if (result == PackageManager.PERMISSION_GRANTED) permissionAction?.invoke()
            else LabModel.state.value = LabState("PERMISSION_DENIED", "Allow TDSpeed in Shizuku, then try again.")
            permissionAction = null
        }
    }
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) { service = (binder as BridgeService.LocalBinder).service }
        override fun onServiceDisconnected(name: ComponentName) { service = null }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bindService(Intent(this, BridgeService::class.java), connection, BIND_AUTO_CREATE)
        Shizuku.addRequestPermissionResultListener(permissionListener)
        setContent {
            val state by LabModel.state.collectAsStateWithLifecycle()
            val preferences = remember { getSharedPreferences("car_lab", MODE_PRIVATE) }
            var allowCarCapture by remember { mutableStateOf(preferences.getBoolean("allowCapture", true)) }
            var reporting by remember { mutableStateOf(false) }
            var reportStatus by remember { mutableStateOf("") }
            ListeningScreen(state, service != null,
                onStart = {
                    withPermission {
                        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
                        startForegroundService(Intent(this, BridgeService::class.java).setAction(BridgeService.ACTION_START)
                            .putExtra("allowCarCapture", allowCarCapture))
                    }
                },
                onStop = { startService(Intent(this, BridgeService::class.java).setAction(BridgeService.ACTION_STOP)) },
                onPitch = { service?.listening(it) },
                onRecenter = { service?.listening(LabModel.state.value.data.getFloat("pitchSemitones"), true) },
                carLab = if (!BuildConfig.CAR_LAB) null else CarLabOptions(allowCarCapture,
                    { allowCarCapture = it; preferences.edit().putBoolean("allowCapture", it).apply() },
                    reportStatus, reporting, {
                        val controller = service
                        if (controller == null) reportStatus = "Wait for the app to connect."
                        else {
                            reporting = true; reportStatus = ""
                            controller.routeReport { text, error ->
                                runOnUiThread {
                                    reporting = false
                                    if (text == null) reportStatus = error ?: "Report unavailable"
                                    else runCatching {
                                        val directory = File(cacheDir, "route-reports").apply { mkdirs() }
                                        directory.listFiles()?.filter { it.name.startsWith("varispeed-route-") && it.extension == "json" }
                                            ?.sortedByDescending { it.lastModified() }?.drop(3)?.forEach { it.delete() }
                                        val report = File(directory, "varispeed-route-${System.currentTimeMillis()}.json")
                                        report.writeText(text)
                                        val uri = FileProvider.getUriForFile(this, "$packageName.reports", report)
                                        reportStatus = "Contains device addresses and app/route details, not recorded audio."
                                        if (!isFinishing && !isDestroyed) startActivity(Intent.createChooser(
                                            Intent(Intent.ACTION_SEND).setType("application/json")
                                                .putExtra(Intent.EXTRA_STREAM, uri)
                                                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION), "Connection report"))
                                    }.onFailure { reportStatus = it.message ?: "Could not share report" }
                                }
                            }
                        }
                    }))
        }
    }

    private fun withPermission(action: () -> Unit) {
        try {
            check(Shizuku.pingBinder()) { "Start Shizuku first, then return here." }
            if (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) action()
            else { permissionAction = action; Shizuku.requestPermission(1) }
        } catch (error: Throwable) { LabModel.state.value = LabState("SHIZUKU_UNAVAILABLE", error.message ?: "Shizuku is not running") }
    }
    override fun onDestroy() {
        Shizuku.removeRequestPermissionResultListener(permissionListener)
        unbindService(connection); super.onDestroy()
    }
}
