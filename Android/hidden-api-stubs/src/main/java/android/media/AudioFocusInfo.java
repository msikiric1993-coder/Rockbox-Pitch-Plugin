package android.media;

/** Compile-only signatures. The framework supplies the runtime class. */
public final class AudioFocusInfo {
    public AudioAttributes getAttributes() { throw new UnsupportedOperationException(); }
    public int getClientUid() { throw new UnsupportedOperationException(); }
    public int getLossReceived() { throw new UnsupportedOperationException(); }
    public String getPackageName() { throw new UnsupportedOperationException(); }
}
