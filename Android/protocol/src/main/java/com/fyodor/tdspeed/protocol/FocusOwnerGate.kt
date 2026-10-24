package com.fyodor.tdspeed.protocol

/** Focus callbacks describe individual clients, not a global playback mute switch. */
class FocusOwnerGate {
    var attenuation = 1f; private set
    private var owner: String? = null

    fun grant(client: String, media: Boolean, granted: Boolean) {
        if (!granted) return
        owner = client
        attenuation = if (media) 1f else 0f
    }

    fun loss(client: String, media: Boolean, mayDuck: Boolean) {
        // A displaced client's loss may arrive after the replacement's grant.
        if (client != owner || !media) return
        attenuation = if (mayDuck) .2f else 0f
    }
}
