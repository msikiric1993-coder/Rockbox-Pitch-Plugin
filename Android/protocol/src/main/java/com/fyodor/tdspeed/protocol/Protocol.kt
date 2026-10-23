package com.fyodor.tdspeed.protocol

object Protocol {
    const val VERSION = 3
    const val SAMPLE_RATE = 48_000
    const val CHANNELS = 2
    const val QUANTUM = 256
    const val HEARTBEAT_MS = 2_000L
    const val LEASE_MS = 10_000L
    const val TRANSPORT_TIMEOUT_MS = 5_000L
}

/** Pure state: no Android clock or Binder dependencies, so stale commands are testable. */
class SessionLease {
    var sessionId = 0L; private set
    var lastCommand = 0L; private set
    var lastHeartbeat = 0L; private set
    val active get() = sessionId != 0L

    fun begin(id: Long, command: Long, now: Long) {
        require(id > 0 && command > 0)
        check(!active) { "A session is already active" }
        sessionId = id; lastCommand = command; lastHeartbeat = now
    }
    fun accept(id: Long, command: Long): Boolean {
        require(id == sessionId && active) { "STALE_SESSION" }
        if (command <= lastCommand) return false
        lastCommand = command
        return true
    }
    fun heartbeat(id: Long, now: Long) {
        require(active && id == sessionId) { "STALE_SESSION" }
        lastHeartbeat = now
    }
    fun expired(now: Long) = active && now - lastHeartbeat >= Protocol.LEASE_MS
    fun end() { sessionId = 0; lastCommand = 0; lastHeartbeat = 0 }
}
