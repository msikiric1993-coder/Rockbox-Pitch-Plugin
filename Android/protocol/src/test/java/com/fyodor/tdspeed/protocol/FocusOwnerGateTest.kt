package com.fyodor.tdspeed.protocol

import org.junit.Assert.assertEquals
import org.junit.Test

class FocusOwnerGateTest {
    @Test fun projectionLossAfterTidalGrantDoesNotMuteTidal() {
        val gate = FocusOwnerGate()
        gate.grant("tidal:old", true, true)
        gate.grant("projection", true, true)
        gate.loss("tidal:old", true, false)
        gate.grant("tidal:new", true, true)
        gate.loss("projection", true, false)
        assertEquals(1f, gate.attenuation, 0f)
    }
    @Test fun distinguishClientsWithinSameUid() {
        val gate = FocusOwnerGate()
        gate.grant("10439:old", true, true)
        gate.grant("10439:new", true, true)
        gate.loss("10439:old", true, false)
        assertEquals(1f, gate.attenuation, 0f)
        gate.loss("10439:new", true, true)
        assertEquals(.2f, gate.attenuation, 0f)
        gate.loss("10439:new", true, false)
        assertEquals(0f, gate.attenuation, 0f)
    }
    @Test fun navigationAndFailedGrantsPreserveProtection() {
        val gate = FocusOwnerGate()
        gate.grant("music", true, true)
        gate.grant("navigation", false, true)
        gate.loss("music", true, true)
        gate.grant("deniedMusic", true, false)
        assertEquals(0f, gate.attenuation, 0f)
        gate.grant("music", true, true)
        gate.loss("navigation", false, false)
        assertEquals(1f, gate.attenuation, 0f)
    }
}
