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
            ListeningScreen(state, service != null,
                onStart = {
                    withPermission {
                        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
                        startForegroundService(Intent(this, BridgeService::class.java).setAction(BridgeService.ACTION_START))
                    }
                },
                onStop = { startService(Intent(this, BridgeService::class.java).setAction(BridgeService.ACTION_STOP)) },
                onPitch = { service?.listening(it) },
                onRecenter = { service?.listening(LabModel.state.value.data.getFloat("pitchSemitones"), true) })
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
