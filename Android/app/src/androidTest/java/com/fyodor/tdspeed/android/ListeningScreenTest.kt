package com.fyodor.tdspeed.android

import android.content.Intent
import android.os.Bundle
import android.view.accessibility.AccessibilityNodeInfo
import androidx.activity.compose.setContent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Presentation-only test. Synthetic UI state never starts a routing session. */
@RunWith(AndroidJUnit4::class)
class ListeningScreenTest {
    @Test fun touchControlsAndHelpRenderWithoutChangingAudio() {
        val i = InstrumentationRegistry.getInstrumentation()
        val activity = i.startActivitySync(Intent(i.targetContext, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        var requested = 99f
        var recentered = false
        var stopped = false
        i.runOnMainSync {
            activity.setContent {
                ListeningScreen(LabState("RUNNING", data = Bundle().apply {
                    putFloat("pitchSemitones", -4f); putDouble("backlogSeconds", 12.5)
                }), true, {}, { stopped = true }, { requested = it }, { recentered = true })
            }
        }
        try {
            Thread.sleep(1200)
            fun find(label: String): AccessibilityNodeInfo {
                fun visit(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
                    if (node.text?.toString() == label || node.contentDescription?.toString() == label) return node
                    for (n in 0 until node.childCount) node.getChild(n)?.let { visit(it) }?.let { return it }
                    return null
                }
                return visit(i.uiAutomation.rootInActiveWindow) ?: error("Missing control: $label")
            }
            fun click(label: String) {
                var node = find(label)
                while (!node.isClickable && node.parent != null) node = node.parent
                val bounds = android.graphics.Rect(); node.getBoundsInScreen(bounds)
                assertTrue("Small touch target: $label", bounds.height() >= (48 * activity.resources.displayMetrics.density).toInt())
                assertTrue(node.performAction(AccessibilityNodeInfo.ACTION_CLICK))
                i.waitForIdleSync()
                Thread.sleep(350) // Allow the next Compose frame and accessibility tree to publish.
            }
            val shot = i.uiAutomation.takeScreenshot()
            File(i.targetContext.filesDir, "touch-ui-running.png").outputStream().use { shot.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
            click("Original pitch"); assertEquals(0f, requested)
            // Scroll the content panel to expose the lower buffer action on smaller screens.
            fun scroll(node: AccessibilityNodeInfo): Boolean {
                if (node.isScrollable) return node.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD)
                for (n in 0 until node.childCount) if (node.getChild(n)?.let { scroll(it) } == true) return true
                return false
            }
            scroll(i.uiAutomation.rootInActiveWindow); Thread.sleep(400)
            click("Re-centre buffer"); assertTrue(recentered)
            click("Stop listening"); assertTrue(stopped)
            // Return to header for Help.
            i.runOnMainSync { activity.recreate() }
            Thread.sleep(1000)
            click("Help"); assertNotNull(find("Listening with TDSpeed")); click("Got it")
        } finally { i.runOnMainSync { activity.finish() } }
    }
}
