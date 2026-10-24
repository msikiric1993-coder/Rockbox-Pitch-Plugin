package com.fyodor.tdspeed.protocol

import org.junit.Assert.*
import org.junit.Test

class PolicyMixSnapshotTest {
    private val base = """
        Audio Policy Mix 1 (0x123):
        - mix type: MIX_TYPE_PLAYERS
        - Route Flags: MIX_ROUTE_FLAG_LOOP_BACK
        - device type: AUDIO_DEVICE_OUT_REMOTE_SUBMIX
        - device address: test:ap:1mixp:0
        - Criterion 0: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_MEDIA
        - Criterion 1: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_GAME
    """.trimIndent()
    @Test fun recognizesOnlyExactOriginalMediaRoute() {
        val mix = PolicyMixSnapshot.parse(base).single()
        assertTrue(mix.projectionMedia)
        assertEquals(PolicyMixSnapshot.expected(emptySet()), mix.signature)
        assertFalse(PolicyMixSnapshot.parse(base.replace("AUDIO_USAGE_GAME", "AUDIO_USAGE_ASSISTANT")).single().projectionMedia)
        assertFalse(PolicyMixSnapshot.parse(base.replace("MIX_ROUTE_FLAG_LOOP_BACK", "MIX_ROUTE_FLAG_LOOP_BACK|MIX_ROUTE_FLAG_RENDER")).single().projectionMedia)
    }
    @Test fun excludesArePreservedInRestorationComparison() {
        val mix = PolicyMixSnapshot.parse(base + "\n - Criterion 2: RULE_EXCLUDE_UID 10439").single()
        assertFalse(mix.projectionMedia)
        assertEquals(PolicyMixSnapshot.expected(setOf(10439)), mix.signature)
        assertNotEquals(PolicyMixSnapshot.expected(setOf(10440)), mix.signature)
    }
    @Test fun separatesNavigationMixAndRejectsMissingDeviceAddress() {
        assertEquals(2, PolicyMixSnapshot.parse(base + "\n" + base.replace("Mix 1", "Mix 2").replace("mixp:0", "mixp:1")).size)
        assertTrue(PolicyMixSnapshot.parse(base.replace("- device address: test:ap:1mixp:0", "")).isEmpty())
    }
    @Test fun identifiesOtherVarispeedInputByExactControllerUidRules() {
        val other = base.replace("AUDIO_USAGE_GAME", "AUDIO_USAGE_UNKNOWN") +
            "\n - Criterion 2: RULE_EXCLUDE_UID 2000\n - Criterion 3: RULE_EXCLUDE_UID 10322"
        val mix = PolicyMixSnapshot.parse(other).single()
        assertTrue(mix.varispeedInputFor(10322))
        assertFalse(mix.varispeedInputFor(10326))
        assertFalse(PolicyMixSnapshot.parse(base).single().varispeedInputFor(10322))
    }
}
