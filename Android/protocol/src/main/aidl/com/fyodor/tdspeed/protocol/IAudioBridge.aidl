package com.fyodor.tdspeed.protocol;
import android.os.Bundle;
import android.os.IBinder;

interface IAudioBridge {
    void destroy() = 16777114;
    Bundle probe(int protocolVersion) = 1;
    void start(long sessionId, long commandId, IBinder owner) = 2;
    void stop(long sessionId, long commandId) = 3;
    void heartbeat(long sessionId) = 4;
    void setDiagnostic(long sessionId, long commandId, float gain, boolean swap) = 5;
    Bundle snapshot() = 6;
    void setListening(long sessionId, long commandId, float semitones, boolean recenter) = 7;
}
