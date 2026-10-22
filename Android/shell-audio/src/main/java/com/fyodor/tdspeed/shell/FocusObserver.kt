package com.fyodor.tdspeed.shell

import android.media.AudioAttributes
import android.media.AudioFocusInfo
import android.media.AudioManager
import android.media.audiopolicy.AudioPolicy
import android.os.SystemClock
import java.util.concurrent.CopyOnWriteArrayList

/** Observes only; never installs a governing focus policy or requests focus from the source. */
internal class FocusObserver : AudioPolicy.AudioPolicyFocusListener() {
    @Volatile var attenuation = 1f; private set
    @Volatile var eventCount = 0L; private set
    private val events = CopyOnWriteArrayList<String>()

    override fun onAudioFocusGrant(info: AudioFocusInfo, requestResult: Int) {
        record("grant", info)
        if (requestResult != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) return
        if (info.attributes.usage in mediaUsages) attenuation = 1f
        // Candidate protection for navigation/calls; P0 traces must establish actual device behaviour.
        else attenuation = 0f
    }
    override fun onAudioFocusLoss(info: AudioFocusInfo, wasNotified: Boolean) {
        record("loss", info)
        if (info.attributes.usage in mediaUsages) {
            attenuation = if (info.lossReceived == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) 0.2f else 0f
        }
    }
    fun traces() = ArrayList(events)
    private fun record(event: String, info: AudioFocusInfo) {
        eventCount++
        events.add("${SystemClock.elapsedRealtime()} $event uid=${info.clientUid} usage=${info.attributes.usage} loss=${info.lossReceived}")
        while (events.size > 32) events.removeAt(0)
    }
    companion object { private val mediaUsages = setOf(AudioAttributes.USAGE_MEDIA, AudioAttributes.USAGE_UNKNOWN) }
}
