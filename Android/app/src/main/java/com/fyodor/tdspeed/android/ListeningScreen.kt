package com.fyodor.tdspeed.android

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import kotlin.math.sin
import kotlin.math.PI
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.pow
import kotlin.math.roundToInt

private val Background = Color(0xff10171b)
private val Panel = Color(0xff1a242a)
private val Ink = Color(0xffeef5f2)
private val Muted = Color(0xff9aafb5)
private val Mint = Color(0xff81dfb5)
private val Border = Color(0xff304047)
private val Yellow = Color(0xffefca72)
private val Red = Color(0xffff8585)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun ListeningScreen(
    state: LabState, connected: Boolean, onStart: () -> Unit, onStop: () -> Unit,
    onPitch: (Float) -> Unit, onRecenter: () -> Unit,
    carLab: CarLabOptions? = null,
) {
    MaterialTheme(colorScheme = darkColorScheme(primary = Mint, onPrimary = Background,
        background = Background, surface = Panel, onSurface = Ink, onSurfaceVariant = Muted,
        secondary = Mint, surfaceVariant = Border, outline = Border)) {
        val running = state.status == "RUNNING"
        val compact = LocalConfiguration.current.screenWidthDp < 380
        val busy = state.status in setOf("CONNECTING", "PREPARING", "STOPPING")
        val active = running || busy
        val pitch = state.data.getFloat("pitchSemitones").coerceIn(-12f, 0f)
        var selected by remember { mutableFloatStateOf(pitch) }
        var dragging by remember { mutableStateOf(false) }
        var pending by remember { mutableStateOf<Float?>(null) }
        LaunchedEffect(pitch, running) {
            if (!running) { selected = pitch; pending = null; dragging = false }
            else if (pending == null || kotlin.math.abs(pitch - pending!!) < .051f) {
                pending = null
                if (!dragging) selected = pitch
            }
        }
        fun requestPitch(value: Float) {
            val next = (value.coerceIn(-12f, 0f) * 10).roundToInt() / 10f
            selected = next; pending = next
            onPitch(next)
        }
        val shown = selected
        val delay = if (running) state.data.getDouble("backlogSeconds", .25).coerceAtLeast(0.0) else 0.0
        val capacity = state.data.getDouble("historyCapacitySeconds", 134217728.0 / 48000.0).coerceAtLeast(.25)
        val fill = (delay / capacity).coerceIn(0.0, 1.0).toFloat()
        val listening = running && state.data.getInt("bufferRecovery") == 0
        var help by remember { mutableStateOf(false) }
        val status = when (state.status) {
            "RUNNING" -> if (state.data.getInt("bufferRecovery") != 0) "Recovering" else "Listening"
            "CONNECTING" -> "Connecting"
            "PREPARING" -> "Preparing buffer"
            "STOPPING" -> "Stopping"
            "STOPPED", "READY" -> "Not listening"
            "DISCONNECTED", "SHIZUKU_UNAVAILABLE" -> "Start Shizuku to listen"
            "PERMISSION_DENIED" -> "Allow access in Shizuku"
            else -> if (state.message.contains("INSUFFICIENT_MEMORY") || state.message.contains("OutOfMemory"))
                "Close some apps to free memory" else "Unable to start · check Shizuku"
        }
        Surface(Modifier.fillMaxSize(), color = Background) {
            Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)
                .padding(horizontal = 20.dp, vertical = 12.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
              Column(Modifier.weight(1f).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("varispeed", fontSize = 23.sp, fontWeight = FontWeight.Bold, letterSpacing = (-1).sp)
                    }
                    TextButton(onClick = { help = true }, modifier = Modifier.sizeIn(minWidth = 56.dp, minHeight = 48.dp)) {
                        Text("Help", color = Muted)
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Canvas(Modifier.size(7.dp)) { drawCircle(if (listening) Mint else Red) }
                    Text("$status (Pitch effect ${if (listening) "on" else "off"})", color = if (listening) Mint else Red, fontSize = 13.sp)
                }
                if (carLab != null) CarLabControls(carLab, state, active)
                Rack {
                    Text("PITCH & SPEED", color = Muted, fontSize = 11.sp, letterSpacing = 1.5.sp)
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                        Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                            Box(Modifier.height(76.dp), contentAlignment = Alignment.BottomCenter) {
                                Text("%.1f".format(shown), color = Ink, fontSize = if (compact) 44.sp else 50.sp,
                                    fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                            }
                            Text("SEMITONES", color = Muted, fontSize = 10.sp, letterSpacing = 1.sp)
                        }
                        Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                            Box(Modifier.height(76.dp), contentAlignment = Alignment.BottomCenter) {
                                Text("%.1f%%".format(100 * 2.0.pow(shown / 12.0)), color = Mint,
                                    fontSize = if (compact) 28.sp else 32.sp, fontWeight = FontWeight.Medium,
                                    fontFamily = FontFamily.Monospace, modifier = Modifier.padding(bottom = 7.dp))
                            }
                            Text("PLAYBACK SPEED", color = Muted, fontSize = 10.sp, letterSpacing = 1.sp)
                        }
                    }
                    StretchVisual(shown, listening)
                    Slider(value = selected,
                        onValueChange = {
                            dragging = true
                            val next = (it * 10).roundToInt() / 10f
                            if (next != selected) requestPitch(next)
                        },
                        onValueChangeFinished = { dragging = false },
                        thumb = {
                            Box(Modifier.size(36.dp).background(if (running) Mint else Muted, CircleShape)
                                .border(3.dp, Background, CircleShape))
                        },
                        valueRange = -12f..0f, enabled = running,
                        colors = SliderDefaults.colors(inactiveTrackColor = Border, inactiveTickColor = Border,
                            activeTickColor = Mint, disabledInactiveTrackColor = Border),
                        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp).heightIn(min = 64.dp)
                            .semantics { contentDescription = "Pitch in semitones" })
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("½ SPEED", color = Muted, fontSize = 10.sp)
                        Text("ORIGINAL", color = Muted, fontSize = 10.sp)
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        TouchButton("−", running, { requestPitch(selected - .1f) }, Modifier.weight(1f), "Lower pitch by 0.1 semitone")
                        TouchButton("Original pitch", running, { requestPitch(0f) }, Modifier.weight(2.6f))
                        TouchButton("+", running, { requestPitch(selected + .1f) }, Modifier.weight(1f), "Raise pitch by 0.1 semitone")
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (value in listOf(-6f, -4f, -2f, -1f)) {
                            val chosen = kotlin.math.abs(selected - value) < .05f
                            OutlinedButton(onClick = { requestPitch(value) }, enabled = running,
                                modifier = Modifier.weight(1f).heightIn(min = 52.dp), shape = RoundedCornerShape(14.dp),
                                border = BorderStroke(1.dp, if (chosen && running) Mint else Border),
                                colors = ButtonDefaults.outlinedButtonColors(contentColor = if (chosen) Mint else Muted)) {
                                Text("%+.0f".format(value), fontFamily = FontFamily.Monospace, fontSize = 16.sp)
                            }
                        }
                    }
                }
                Rack {
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("BUFFER", color = Muted, fontSize = 11.sp, letterSpacing = 1.5.sp)
                        Text("%.1f s delay".format(delay), color = Yellow, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
                    }
                    LinearProgressIndicator(progress = { fill }, modifier = Modifier.fillMaxWidth().height(6.dp)
                        .semantics { contentDescription = "Buffer filled %.2f percent".format(fill * 100) },
                        color = Yellow, trackColor = Border)
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("%.2f%% filled".format(fill * 100), color = Muted, fontSize = 10.sp)
                        Text("1 GiB", color = Muted, fontSize = 10.sp)
                    }
                    OutlinedButton(onClick = onRecenter, enabled = running,
                        modifier = Modifier.fillMaxWidth().heightIn(min = 60.dp), shape = RoundedCornerShape(16.dp),
                        border = BorderStroke(1.dp, if (running) Yellow.copy(alpha = .65f) else Border),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = Yellow)) {
                        Text("Re-centre buffer", fontSize = 17.sp, fontWeight = FontWeight.Medium)
                    }
                    if (running && state.data.getInt("bufferRecovery") != 0)
                        Text("Resuming in %.1f s".format(state.data.getDouble("recoverySeconds")), color = Yellow)
                }
              }
                Button(onClick = if (active) onStop else onStart, enabled = active || connected,
                    modifier = Modifier.fillMaxWidth().heightIn(min = 64.dp), shape = RoundedCornerShape(18.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = if (active) Border else Mint,
                        contentColor = if (active) Ink else Background)) {
                    Text(if (active) "Stop listening" else "Start listening", fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
                }
            }
        }
        if (help) AlertDialog(onDismissRequest = { help = false }, title = { Text("Listening with Varispeed") },
            text = { Text("Start Shizuku, play music in your source app, then tap Start listening.\n\nKeep Developer options and USB debugging enabled; no cable or PC is needed while Shizuku is running. Start Shizuku again after reboot.\n\nPitch and speed change together. The slider goes from −12 to 0 semitones. Original pitch keeps your delay; Re-centre jumps near live. The two waveforms illustrate the same segment: the lower one stretches in time as you slow playback. They are not live audio. Buffer fill is the accumulated delay divided by the full history capacity.\n\nThe 1 GiB buffer is held in memory. Stop clears it. Android Auto support is still being investigated.") },
            confirmButton = { TextButton(onClick = { help = false }) { Text("Got it") } })
    }
}

@Composable
private fun Rack(content: @Composable ColumnScope.() -> Unit) {
    Surface(color = Panel, shape = RoundedCornerShape(22.dp), border = BorderStroke(1.dp, Border)) {
        Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp), content = content)
    }
}

@Composable
private fun TouchButton(label: String, enabled: Boolean, action: () -> Unit, modifier: Modifier,
    description: String = label) {
    OutlinedButton(onClick = action, enabled = enabled,
        modifier = modifier.heightIn(min = 58.dp).semantics { contentDescription = description },
        shape = RoundedCornerShape(14.dp), border = BorderStroke(1.dp, Border), contentPadding = PaddingValues(8.dp)) {
        Text(label, fontSize = if (label.length == 1) 25.sp else 15.sp, maxLines = 1)
    }
}


/** Same illustrative segment shown on a common time scale; no audio-thread work. */
@Composable
private fun StretchVisual(semitones: Float, enabled: Boolean) {
    val rate = 2.0.pow(semitones / 12.0).toFloat()
    val stretch = 1f / rate
    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
        Text("SAME SEGMENT · %.0f%% LONGER".format((stretch - 1) * 100), color = Muted, fontSize = 9.sp)
        Canvas(Modifier.fillMaxWidth().height(40.dp).semantics {
            contentDescription = "Illustration: the same audio segment takes %.2f times as long".format(stretch)
        }) {
            fun segment(y: Float, width: Float, color: Color) {
                val path = Path()
                for (i in 0..180) {
                    val t = i / 180f
                    val envelope = sin(t * PI.toFloat())
                    val v = sin(t * 10 * PI.toFloat()) * envelope
                    val pointY = y - v * size.height * .16f
                    if (i == 0) path.moveTo(0f, pointY) else path.lineTo(t * width, pointY)
                }
                drawLine(Border, Offset(0f, y), Offset(size.width, y), 1.dp.toPx())
                drawPath(path, color, style = Stroke(2.dp.toPx(), cap = StrokeCap.Round))
                drawLine(color, Offset(width, y - 9.dp.toPx()), Offset(width, y + 9.dp.toPx()), 1.dp.toPx())
            }
            val base = size.width * .46f
            segment(size.height * .25f, base, Muted)
            segment(size.height * .75f, base * stretch, if (enabled) Yellow else Muted.copy(alpha = .4f))
        }
        Text("Grey: original · Yellow: slowed (illustration)", color = Muted, fontSize = 9.sp)
    }
}
