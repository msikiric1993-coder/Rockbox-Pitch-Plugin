package com.fyodor.tdspeed.protocol

import org.junit.Assert.*
import org.junit.Test

class ProjectionDiscoveryTest {
    private val car = "2068906430:ap:238mixp:0"
    private val other = "-2022842524:ap:247mixp:0"
    private val event = "10-24 14:34:56:540 registerAudioPolicy for binder u/pid:10246/23498 with config:reg:2068906430:ap:238 Mix:0-Typ:p-Rul:2"
    private val rolled = "10-24 14:42:13:796 registerAudioPolicy for other u/pid:2000/28689 with config:reg:-2022842524:ap:247 Mix:0-Typ:p-Rul:4"
    @Test fun reportSequenceRetainsCarAcrossRolloverAndNewGuardian() {
        val initial = ProjectionDiscovery().discover(10246, event, setOf(car, other))
        assertEquals(setOf(car), initial)
        val restarted = ProjectionDiscovery(initial)
        assertEquals(setOf(car), restarted.discover(10246, rolled, setOf(car, other)))
        assertEquals("verified_connection_cache", restarted.evidence)
    }
    @Test fun acceptsCurrentProjectionRecorderWhenRegistrationEventIsGone() {
        assertEquals(setOf(car), ProjectionDiscovery().discover(10246, rolled, setOf(car, other), setOf(car)))
    }
    @Test fun neverGuessesFromAnUnownedMixAndDropsDisconnectedAddresses() {
        assertTrue(ProjectionDiscovery().discover(10246, rolled, setOf(car, other)).isEmpty())
        val discovery = ProjectionDiscovery(setOf(car))
        assertTrue(discovery.discover(10246, "", setOf(other)).isEmpty())
        assertTrue(discovery.discover(10246, "", setOf(car, other)).isEmpty())
    }
    @Test fun requiresCorrectUidAndExactRegistrationPrefix() {
        assertTrue(ProjectionDiscovery().discover(10247, event, setOf(car)).isEmpty())
        assertTrue(ProjectionDiscovery().discover(10246, event, setOf("2068906430:ap:2389mixp:0")).isEmpty())
        assertTrue(ProjectionDiscovery().discover(10246, event.replace("registerAudioPolicy", "unregisterAudioPolicy"), setOf(car)).isEmpty())
    }
}
