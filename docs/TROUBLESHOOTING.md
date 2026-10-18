# Troubleshooting and remaining release gates

- No sound: check Audio settings for a usable paired cable and a physical output. The app starts forwarding dry automatically. Use Stop and restore Windows audio from the tray for normal playback if transport cannot open.
- Another TDSpeed is forwarding: Exit the older app from its tray. Closing a window only hides it. Preview and older settings/installations remain separate.
- Tidal/browser choosing another output or exclusive mode: select the cable in that app or Windows per-app sound settings. Global default changes cannot override an explicitly chosen output.
- Temporary output: your saved device is unavailable. The app uses an active physical fallback and retains your preference; it does not jump back mid-song.
- Audio device disconnected: processing is suspended and owned routes restored to an available physical destination. A retained recovery journal retries failed restores. A manual Windows output change is respected.
- Growing delay at lower pitch: expected varispeed. The song is longer than the incoming stream. There is no transparent live algorithm that both slows playback and avoids retaining those extra samples. Re-centre skips buffered audio; the history limit uses the requested three-second pause/reset.
- Reverb sounds dry at 100%: check the slot is On, effects are not bypassed, processing is enabled, and no effect/IR error is shown. Mix 100% is wet-only. Clear a latched fault with Reset effect; load a missing IR explicitly.
- Clipping: reduce Slot Output, Wet Level or Final Output. Protect peaks is optional clipping protection, not gain normalization.
- Save failed: the appdata folder must be writable. The existing rack file is retained when an atomic write fails. An existing rack name is updated rather than duplicated.
- UI sizing: use the whole-app size menu. Expanded controls grow/shrink the fixed window automatically and scale down to fit the current monitor. No panel is independently draggable/resizable.

Files live in %APPDATA%/TDSpeed Preview. Startup entry: HKCU Run/TDSpeed Preview. Regular %APPDATA%/TDSpeed is read only for a one-time copy. Recovery journal is routing.json. --restore attempts ownership-aware restoration; Windows sound settings remain available if no supported output is connected.

## Publication acceptance

Listen to the supplied full-wet comparison renders and space variants on your own music. Confirm musical character, usable Mix balance and preset defaults. This subjective acceptance cannot be inferred from signal tests.

Check a fresh Windows 11 x64 installation without cables/ASIO, full signed-driver fallback including restart, USB/Bluetooth physical disconnect/reconnect, multiple monitors at Windows DPI 100/125/150/200%, and the intended DAW host. Real hardware tests on this PC do not substitute for these checks. Verify Tidal/browser playback by listening as well as diagnostic tone loopback. These environment-specific checks remain documented limitations; no two-hour soak is required.
