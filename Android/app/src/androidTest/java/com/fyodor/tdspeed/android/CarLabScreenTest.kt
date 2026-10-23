package com.fyodor.tdspeed.android

import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import org.junit.Rule
import org.junit.Test
import org.junit.Assert.*

class CarLabScreenTest {
    @get:Rule val ui = createAndroidComposeRule<MainActivity>()

    @Test fun captureOptionLocksDuringPlaybackAndReportIsExplicit() {
        var active by mutableStateOf(false)
        var capture by mutableStateOf(true)
        var reports = 0
        ui.activity.setContent {
            CarLabControls(CarLabOptions(capture, { capture = it }, "", false, { reports++ }), LabState(), active)
        }
        val toggle = ui.onNodeWithContentDescription("Allow processed audio to be captured")
        toggle.assertIsOn().performClick().assertIsOff()
        ui.runOnIdle { assertFalse(capture); active = true }
        toggle.assertIsNotEnabled()
        ui.onNodeWithText("Share connection report").performClick()
        ui.runOnIdle { assertEquals(1, reports) }
    }
}
