package com.fyodor.tdspeed.android

import android.Manifest
import android.content.ComponentName
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.os.IBinder
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import rikka.shizuku.Shizuku
import kotlin.math.pow
import kotlin.math.roundToInt

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
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(Modifier.fillMaxSize()) {
                    val state by LabModel.state.collectAsStateWithLifecycle()
                    val data = state.data
                    val playing = state.status == "RUNNING"
                    val busy = state.status in setOf("CONNECTING", "PREPARING", "STOPPING")
                    val active = playing || busy
                    val pitch = data.getFloat("pitchSemitones")
                    var selected by remember { mutableFloatStateOf(0f) }
                    var dragging by remember { mutableStateOf(false) }
                    LaunchedEffect(pitch) { if (!dragging) selected = pitch }
                    val status = when (state.status) {
                        "RUNNING" -> if (data.getInt("bufferRecovery") != 0) "Recovering buffer…" else "Listening"
                        "PREPARING" -> "Preparing 1 GiB buffer…"
                        "CONNECTING" -> "Connecting to Shizuku…"
                        "STOPPING" -> "Stopping…"
                        "STOPPED", "READY" -> "Stopped"
                        "PERMISSION_DENIED" -> "Allow TDSpeed in Shizuku."
                        "DISCONNECTED", "SHIZUKU_UNAVAILABLE" -> "Start Shizuku, then tap Start."
                        "UNSUPPORTED" -> "Audio processing is unavailable on this phone."
                        else -> if (state.message.contains("INSUFFICIENT_MEMORY") || state.message.contains("OutOfMemory"))
                            "Not enough free memory. Close some apps and try again."
                        else "Could not start. Check that Shizuku is running, then try again."
                    }
                    Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)
                        .verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                        Text("TDSpeed", style = MaterialTheme.typography.headlineLarge)
                        Text("Pitch & speed", style = MaterialTheme.typography.titleMedium)
                        Text(status, style = MaterialTheme.typography.bodyLarge)
                        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            Button(onClick = {
                                withPermission {
                                    if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                                        notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
                                    startForegroundService(Intent(this@MainActivity, BridgeService::class.java).setAction(BridgeService.ACTION_START))
                                }
                            }, enabled = !active && service != null, modifier = Modifier.weight(1f)) { Text("Start") }
                            OutlinedButton(onClick = {
                                startService(Intent(this@MainActivity, BridgeService::class.java).setAction(BridgeService.ACTION_STOP))
                            }, enabled = active, modifier = Modifier.weight(1f)) { Text("Stop") }
                        }
                        ElevatedCard(Modifier.fillMaxWidth()) {
                            Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                Text("%+.1f semitones".format(if (dragging) selected else pitch), style = MaterialTheme.typography.headlineMedium)
                                Text("%.1f%% speed".format(100.0 * 2.0.pow(pitch / 12.0)), style = MaterialTheme.typography.titleMedium)
                                Slider(value = selected, onValueChange = { dragging = true; selected = (it * 10).roundToInt() / 10f },
                                    onValueChangeFinished = { dragging = false; service?.listening(selected) },
                                    valueRange = -12f..12f, steps = 239, enabled = playing)
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    OutlinedButton(onClick = { service?.listening(-12f) }, enabled = playing, modifier = Modifier.weight(1f)) { Text("−12") }
                                    OutlinedButton(onClick = { service?.listening(-4f) }, enabled = playing, modifier = Modifier.weight(1f)) { Text("−4") }
                                    OutlinedButton(onClick = { service?.listening(4f) }, enabled = playing, modifier = Modifier.weight(1f)) { Text("+4") }
                                }
                                OutlinedButton(onClick = { service?.listening(0f) }, enabled = playing, modifier = Modifier.fillMaxWidth()) { Text("Original pitch") }
                                OutlinedButton(onClick = { service?.listening(pitch, true) }, enabled = playing, modifier = Modifier.fillMaxWidth()) { Text("Re-centre buffer") }
                            }
                        }
                        Text("Delay: %.2f s".format(if (active) data.getDouble("backlogSeconds", 0.25) else 0.0), style = MaterialTheme.typography.titleMedium)
                        Text("1 GiB memory buffer", style = MaterialTheme.typography.bodyMedium)
                        if (data.getInt("bufferRecovery") != 0 && playing)
                            Text("Restarting near live in %.1f s".format(data.getDouble("recoverySeconds")))
                        Text("Pitch and speed change together. Original pitch keeps the delay; re-centre jumps near live.")
                        Text("Stop clears the buffer. Audio is never saved to a file.", style = MaterialTheme.typography.bodySmall)
                        TextButton(onClick = { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://shizuku.rikka.app/guide/setup/"))) }) { Text("Shizuku setup") }
                    }
                }
            }
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
