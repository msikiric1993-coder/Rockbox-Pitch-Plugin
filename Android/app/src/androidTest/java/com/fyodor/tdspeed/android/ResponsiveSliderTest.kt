package com.fyodor.tdspeed.android

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import org.junit.Rule
import org.junit.Test
import org.junit.Assert.*
import java.util.concurrent.atomic.AtomicReference

class ResponsiveSliderTest {
    @get:Rule val ui = createAndroidComposeRule<MainActivity>()

    @Test fun dragSendsPitchBeforeReleaseAndIgnoresStaleSnapshot() {
        val requested = AtomicReference(0f)
        ui.activity.setContent {
            ListeningScreen(LabState("RUNNING", data = Bundle().apply { putFloat("pitchSemitones", 0f) }),
                true, {}, {}, { requested.set(it) }, {})
        }
        val slider = ui.onNodeWithContentDescription("Pitch in semitones")
        slider.performTouchInput {
            down(Offset(width * .9f, centerY))
            moveTo(Offset(width * .7f, centerY), 100)
        }
        ui.runOnIdle { assertTrue("No pitch update while finger held", requested.get() < -1f) }
        val first = requested.get()
        slider.performTouchInput { moveTo(Offset(width * .4f, centerY), 100) }
        ui.runOnIdle { assertTrue("Drag did not continue updating", requested.get() < first) }
        val held = requested.get()
        slider.performTouchInput { up() }
        ui.onNodeWithText("%.1f".format(held)).assertExists()
        assertTrue(held in -12f..0f)
    }
}
