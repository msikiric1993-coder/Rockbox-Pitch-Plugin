package com.fyodor.tdspeed.testsource

import android.app.Activity
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.sin

/** Local synthetic content. Keeps playing when the routing-lab Activity is opened. */
class SourceActivity : Activity() {
    private val active = AtomicBoolean(false)
    private var track: AudioTrack? = null
    private var thread: Thread? = null
    private var focus: AudioFocusRequest? = null
    private lateinit var status: TextView
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val layout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(40, 80, 40, 30) }
        layout.addView(TextView(this).apply { text = "TDSpeed stereo test source\n\nLeft: 440 Hz · Right: 660 Hz\nQuiet synthetic tones, 48 kHz stereo. Start here, then open TDSpeed Routing Lab. No music service is involved."; textSize = 20f })
        status = TextView(this).apply { text = "Stopped" }; layout.addView(status)
        layout.addView(Button(this).apply { text = "Play stereo tones"; setOnClickListener { startTones() } })
        layout.addView(Button(this).apply { text = "Stop tones"; setOnClickListener { stopTones() } })
        setContentView(layout)
    }
    private fun startTones() {
        if (active.get()) return
        try {
            val manager = getSystemService(AudioManager::class.java)
            val attributes = AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).setAllowedCapturePolicy(AudioAttributes.ALLOW_CAPTURE_BY_ALL).build()
            val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).setAudioAttributes(attributes)
                .setOnAudioFocusChangeListener { change -> if (change == AudioManager.AUDIOFOCUS_LOSS) stopTones() }.build()
            check(manager.requestAudioFocus(request) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) { "Audio focus denied" }; focus = request
            val output = AudioTrack.Builder().setAudioAttributes(attributes)
                .setAudioFormat(AudioFormat.Builder().setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_FLOAT).build())
                .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(16_384).build()
            track = output; output.play(); active.set(true)
            thread = Thread({
                val samples = FloatArray(512); var frame = 0L
                try {
                    while (active.get()) {
                        for (i in 0 until 256) {
                            samples[2*i] = (0.08 * sin(2 * Math.PI * 440 * frame / 48000)).toFloat()
                            samples[2*i+1] = (0.08 * sin(2 * Math.PI * 660 * frame / 48000)).toFloat(); frame++
                        }
                        var offset = 0
                        while (offset < samples.size && active.get()) {
                            val written = output.write(samples, offset, samples.size - offset, AudioTrack.WRITE_BLOCKING)
                            check(written > 0); offset += written
                        }
                    }
                } catch (_: Throwable) { if (active.get()) runOnUiThread { stopTones(); status.text = "Audio output failed" } }
            }, "TDSpeed-test-tones").also { it.start() }
            status.text = "Playing · left 440 Hz / right 660 Hz"
        } catch (error: Throwable) { stopTones(); status.text = error.message }
    }
    private fun stopTones() {
        active.set(false); runCatching { track?.stop() }; thread?.join(1000); thread = null
        track?.release(); track = null
        focus?.let { getSystemService(AudioManager::class.java).abandonAudioFocusRequest(it) }; focus = null
        status.text = "Stopped"
    }
    override fun onDestroy() { stopTones(); super.onDestroy() }
}
