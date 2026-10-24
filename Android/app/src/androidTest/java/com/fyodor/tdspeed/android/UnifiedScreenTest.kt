package com.fyodor.tdspeed.android

import android.os.Bundle
import androidx.compose.ui.test.*
import androidx.activity.compose.setContent
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class UnifiedScreenTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()
    @Test fun regularScreenHasOneListeningControlAndReportInHelp() {
        var report = false
        compose.activity.setContent {
            ListeningScreen(LabState("RUNNING", data = Bundle().apply { putString("carRouteStatus", "REROUTED") }),
                true, {}, {}, {}, {}, connectionReport = ConnectionReportOptions("", false) { report = true })
        }
        compose.onNodeWithText("Android Auto · routed").assertIsDisplayed()
        compose.onNodeWithText("Android Auto experiment").assertDoesNotExist()
        compose.onNodeWithText("Stop listening").assertIsDisplayed()
        compose.onNodeWithText("Help").performClick()
        compose.onNodeWithText("Share connection report").performScrollTo().performClick()
        compose.runOnIdle { assertTrue(report) }
    }
}
