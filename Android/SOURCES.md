# TDSpeed Android evidence and source baseline

Checked on 20 October 2026. Links support platform facts and implementation precedents. Thread, storage, UI and acceptance choices are TDSpeed design decisions. No physical Android or car compatibility result is established by these references.

## Minimum Android version

The examined AOSP shell manifest declares MODIFY_AUDIO_ROUTING in Android 13. The examined Android 12 manifest does not; both declare CAPTURE_AUDIO_OUTPUT. This is the reason for minSdk 33 in the initial routing product. These tagged AOSP files do not establish every OEM build's permissions.

- [Android 13 shell manifest](https://github.com/aosp-mirror/platform_frameworks_base/blob/android-13.0.0_r1/packages/Shell/AndroidManifest.xml)
- [Android 12 shell manifest](https://github.com/aosp-mirror/platform_frameworks_base/blob/android-12.0.0_r1/packages/Shell/AndroidManifest.xml)

## Routing and privileges

Shizuku UserService can execute Java and JNI code in a separate process under shell UID 2000 when started through ADB. Use that mechanism and keep private APIs in the helper. [Shizuku API developer guide](https://github.com/RikkaApps/Shizuku-API#userservice).

Shizuku's unrooted setup supports wireless-debugging startup on Android 11+ and requires startup again after reboot. Device-specific restrictions can affect it. [Official user manual](https://shizuku.rikka.app/guide/setup/).

Android's public AudioPlaybackCapture copies eligible playback and requires capture-policy permission and user consent. It does not establish the replacement route required here. [Android capture documentation](https://developer.android.com/media/platform/av-capture).

The examined voice-isolation project is an implementation precedent for pure loopback, helper-UID exclusion and shell attribution. Its published verification includes an Android 16 emulator. It is a speech-oriented app and does not establish stereo music quality, Samsung compatibility or Android Auto operation. Its stated limitations include protected/offloaded playback and OEM routing differences. [Project description](https://github.com/sk2andy/android-realtime-voice-isolation).

Pin any reused routing code to examined commit **71b084aa98dc620bd2b477cf9f721c27b1629d24**, then review its MIT licence and actual behaviour:

- [Audio policy implementation](https://github.com/sk2andy/android-realtime-voice-isolation/blob/71b084aa98dc620bd2b477cf9f721c27b1629d24/app/src/main/java/dev/sk2andy/voiceisolation/audio/PrivilegedAudioPolicy.kt)
- [Audio pipeline](https://github.com/sk2andy/android-realtime-voice-isolation/blob/71b084aa98dc620bd2b477cf9f721c27b1629d24/app/src/main/java/dev/sk2andy/voiceisolation/audio/PrivilegedAudioPipeline.kt)
- [Shell attribution adapter](https://github.com/sk2andy/android-realtime-voice-isolation/blob/71b084aa98dc620bd2b477cf9f721c27b1629d24/app/src/main/java/dev/sk2andy/voiceisolation/audio/ShellAttributionContext.kt)
- [UserService implementation](https://github.com/sk2andy/android-realtime-voice-isolation/blob/71b084aa98dc620bd2b477cf9f721c27b1629d24/app/src/main/java/dev/sk2andy/voiceisolation/service/PrivilegedIsolationUserService.kt)

No external source code has been copied into this architecture package. Its proposed stereo transport, history provider and DSP integration require implementation.

## Foreground lifetime

Android documents mediaPlayback for ongoing background audio/video output. mediaProcessing covers operations such as format conversion and has different lifecycle limits. Apps targeting Android 15+ cannot start a mediaPlayback foreground service from BOOT_COMPLETED. These rules inform the proposed foreground wrapper; they do not verify the helper's Samsung lifecycle. [Foreground service types](https://developer.android.com/develop/background-work/services/fgs/service-types).

## Audio focus and source control

Android can fade a playing source when a second app requests permanent audio focus. On target API 35+, focus requests require a top application or an applicable foreground service. This makes a second ordinary player a poor default for our external-source route. [Audio focus guide](https://developer.android.com/media/optimize/audio-focus).

The AOSP AudioPolicy class exposes focus observation separately from declaring a policy that governs focus decisions. Observation in the proposed helper is an engineering candidate that needs device validation. [Tagged Android 13 AudioPolicy source](https://github.com/aosp-mirror/platform_frameworks_base/blob/android-13.0.0_r1/media/java/android/media/audiopolicy/AudioPolicy.java).

MediaSessionManager restricts access to other active sessions to authorized callers, including suitable privileged permission or enabled notification listeners. Request only the access needed for the chosen source-control path. [MediaSessionManager reference](https://developer.android.com/reference/android/media/session/MediaSessionManager).

## Android Auto

MediaLibraryService and MediaSession provide a phone media app's car integration. The platform supplies the car UI. [Android for Cars media architecture](https://developer.android.com/media/implement/surfaces/cars).

The custom native pipeline can be represented by a Player adapter. SimpleBasePlayer is the proposed implementation base. [SimpleBasePlayer reference](https://developer.android.com/reference/androidx/media3/common/SimpleBasePlayer).

Media sessions support custom actions, with host-dependent placement and overflow. That supports a proposed re-centre action; it does not let TDSpeed insert buttons into another app's car screen. [Playback controls](https://developer.android.com/training/cars/media/enable-playback).

Android Auto discovery requires manifest media declarations. Sideloaded media testing may need Android Auto's unknown-sources developer option; trusted testing distribution is another route. [Manifest integration](https://developer.android.com/training/cars/media/auto), [car testing](https://developer.android.com/training/cars/testing).

## Samsung installation

Samsung documents Auto Blocker's restrictions on outside-store apps and USB commands. Actual restrictions and settings must be checked on the owner's One UI build. [Samsung Auto Blocker](https://www.samsung.com/ca/support/mobile-devices/protect-your-samsung-galaxy-with-auto-blocker/).

## Native packaging

Android supports 16 KiB page-size devices and native libraries need compatible building and packaging. Follow the current AGP/NDK guidance and inspect every bundled native library. This design makes no claim about a specific S24 Ultra page size. [Android native page-size guidance](https://developer.android.com/guide/practices/page-sizes).

AudioRecord and AudioTrack provide the PCM transport interfaces. Their platform APIs do not guarantee that a selected private capture policy or car route works. [AudioRecord builder](https://developer.android.com/reference/android/media/AudioRecord.Builder), [AudioTrack](https://developer.android.com/reference/android/media/AudioTrack).

## Local engine baseline

The reference examined for extraction is `D:/Code/TDSpeedVST/RC build/0.4.2-rc.1/project`. This selection identifies source inputs and does not assert that it is the only working Windows build. The workspace root also contains an older separate project; do not choose it accidentally because its paths are shorter.

The examined processor uses coupled rate `2^(semitones/12)`, four-point Hermite interpolation, approximately 1 ms rate smoothing, a 250 ms target offset and three-second full/empty recovery. Its stereo history limit is 2 GiB. The Windows WASAPI transport applies a separate bounded clock correction. Android must replace the platform transport and redesign history allocation while preserving the chosen listening algorithm.

| Relative path under the reference project | SHA-256 |
|---|---|
| Source/PluginProcessor.cpp | 34af8c138f5ad618ca7c77835fc4b8f8aaf901cbb42cf6c643de72a04977d02c |
| Source/PluginProcessor.h | 391eb53d784b9186c883f2d35dbf86f850fc387a9d394266df961fc54fbbc175 |
| Source/EffectsRack.cpp | adaee94ee2225a803cfc8ee8abfa63be5e60942057fdcf3e6e262f547fc3db49 |
| Source/EffectsRack.h | b8333886e6694ac52511b671fce6d85291fb1e980406a078226377943954b7e3 |
| Source/FractionalDelay.h | 19348f702034043149ba330344ceb08142394fbc606606ed970ee6e3e6afa3f4 |
| Source/Standalone/WasapiEngine.cpp | 3e6c5dd34471a6ae4465d888ff957f44242da7ab124ba244e6dc214260dc49ac |
| CMakeLists.txt | 2a644437f0b40ce86c6ace1ea14acb68f600ed69a781f4d083b6e51cdaae0695 |
| LICENSE | 0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0 |
| Source/ThirdParty/VERSIONS.md | ac27f1c14873f06f4af3e9990b00fded6de4e0cebe4b50c9a4b037e4e911e29d |

The project includes AGPL-3.0 text and component-specific third-party notices. The bundled JUCE licence describes AGPLv3/commercial alternatives. Preserve the actual notices and corresponding source for the chosen Android dependency set; extracting DSP does not itself change its licence. Inspect the local LICENSE, JUCE/LICENSE.md and Source/ThirdParty/VERSIONS.md when packaging.
