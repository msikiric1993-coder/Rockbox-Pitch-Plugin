package com.fyodor.tdspeed.protocol

/** Retain verified identities while the exact native mixes still exist. Events are a bounded log. */
class ProjectionDiscovery(cached: Set<String> = emptySet()) {
    private val verified = cached.toMutableSet()
    var evidence = "none"; private set
    fun discover(projectionUid: Int, audioDump: String, liveAddresses: Set<String>,
        recordingAddresses: Set<String> = emptySet()): Set<String> {
        val registrations = Regex("(?m)^.*\\bregisterAudioPolicy for [^\\n]*u/pid:$projectionUid/\\d+ with config:reg:([^ \\r\\n]+)")
            .findAll(audioDump).map { it.groupValues[1] }.toSet()
        val fromEvents = liveAddresses.filter { address -> registrations.any { registration ->
            Regex("${Regex.escape(registration)}mixp:\\d+").matches(address)
        } }.toSet()
        val fromRecorders = recordingAddresses.intersect(liveAddresses)
        verified.retainAll(liveAddresses)
        val retained = verified.isNotEmpty()
        verified.addAll(fromEvents); verified.addAll(fromRecorders)
        evidence = when {
            fromEvents.isNotEmpty() -> "registration_event"
            fromRecorders.isNotEmpty() -> "active_projection_recorder"
            retained -> "verified_connection_cache"
            else -> "none"
        }
        return verified.toSet()
    }
}
