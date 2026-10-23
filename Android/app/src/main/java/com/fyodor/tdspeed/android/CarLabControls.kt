package com.fyodor.tdspeed.android

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics

internal data class CarLabOptions(val allowCapture: Boolean, val onCaptureChange: (Boolean) -> Unit,
    val reportStatus: String, val reporting: Boolean, val onReport: () -> Unit)

@Composable
internal fun CarLabControls(options: CarLabOptions, state: LabState, active: Boolean) {
    Surface(color = Color(0xff1a242a), shape = RoundedCornerShape(18.dp), border = BorderStroke(1.dp, Color(0xffefca72))) {
        Column(Modifier.fillMaxWidth().padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Android Auto experiment", color = Color(0xffefca72))
            Text("Park first. Stop regular Varispeed. Disconnect Android Auto → Start here → reconnect Android Auto. Then try −4.", fontSize = 13.sp)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Allow processed audio to be captured", Modifier.weight(1f), fontSize = 13.sp)
                Switch(checked = options.allowCapture, onCheckedChange = options.onCaptureChange, enabled = !active,
                    modifier = Modifier.semantics { contentDescription = "Allow processed audio to be captured" })
            }
            Text("For Android Auto; also permits other authorized playback capture. Change while stopped.", fontSize = 11.sp)
            if (state.status == "RUNNING") Text(
                if (state.data.getFloat("inputPeak") > .00001f) "Music is reaching Varispeed; confirm the effect in the car."
                else "No input signal right now. Music may be paused, or the route may be bypassing Varispeed.", fontSize = 12.sp)
            Text("Reports include app names and device addresses; no recorded audio.", fontSize = 11.sp)
            OutlinedButton(onClick = options.onReport, enabled = !options.reporting,
                modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                Text(if (options.reporting) "Collecting report…" else "Share connection report")
            }
            if (options.reportStatus.isNotEmpty()) Text(options.reportStatus, fontSize = 12.sp)
            Text("Experimental · car playback not yet verified", fontSize = 11.sp)
        }
    }
}
