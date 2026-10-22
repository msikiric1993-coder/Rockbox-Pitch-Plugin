package com.fyodor.tdspeed.protocol

import org.junit.Assert.*
import org.junit.Test

class SessionLeaseTest {
    @Test fun duplicatesAndReorderedCommandsDoNotApply() {
        val lease = SessionLease(); lease.begin(7, 1, 0)
        assertTrue(lease.accept(7, 3))
        assertFalse(lease.accept(7, 3)); assertFalse(lease.accept(7, 2))
    }
    @Test fun heartbeatExtendsOnlyItsOwnSession() {
        val lease = SessionLease(); lease.begin(7, 1, 100)
        assertFalse(lease.expired(10_099)); assertTrue(lease.expired(10_100))
        lease.heartbeat(7, 5_000)
        assertFalse(lease.expired(14_999)); assertTrue(lease.expired(15_000))
        assertThrows(IllegalArgumentException::class.java) { lease.heartbeat(8, 20_000) }
    }
    @Test fun staleCommandsCannotAffectNextSession() {
        val lease = SessionLease(); lease.begin(7, 1, 0); lease.end(); lease.begin(8, 1, 10)
        assertThrows(IllegalArgumentException::class.java) { lease.accept(7, 50) }
        assertTrue(lease.accept(8, 2))
    }
    @Test fun activeSessionCannotBeReplacedByAnotherStart() {
        val lease = SessionLease(); lease.begin(7, 1, 0)
        assertThrows(IllegalStateException::class.java) { lease.begin(8, 1, 5) }
        assertEquals(7L, lease.sessionId)
    }
}
