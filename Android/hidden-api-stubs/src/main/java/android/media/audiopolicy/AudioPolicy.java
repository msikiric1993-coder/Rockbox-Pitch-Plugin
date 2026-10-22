package android.media.audiopolicy;
import android.media.AudioFocusInfo;

/** Must be compileOnly. Shipping this class would shadow the actual platform API. */
public class AudioPolicy {
    public static abstract class AudioPolicyFocusListener {
        public void onAudioFocusGrant(AudioFocusInfo info, int requestResult) {}
        public void onAudioFocusLoss(AudioFocusInfo info, boolean wasNotified) {}
    }
}
