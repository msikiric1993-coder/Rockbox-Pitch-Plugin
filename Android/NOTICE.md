# Source notices

The TDSpeed Android source follows the existing TDSpeed project's AGPL-3.0 licensing baseline. See LICENSE. The P1 dry varispeed core is extracted from the pinned Windows 0.4.2-rc.1 PluginProcessor.cpp. It preserves the listening algorithm and adds Android-independent buffer contracts and invalid-PCM containment. The Windows processor remains unchanged. The reference harness links its existing JUCE shared-code library for verification only; the Android dry core has no JUCE dependency.

The pure loopback and shell-attribution adapters were informed by and adapted from the MIT-licensed android-realtime-voice-isolation project by André Naumann, examined at revision 71b084aa98dc620bd2b477cf9f721c27b1629d24. Its MIT notice is retained in LICENSES/voice-isolation-MIT.txt. Its speech model and ML runtime are not included.

Shizuku API and provider dependencies are maintained by RikkaApps and licensed under MIT. The Shizuku manager is a separate installation; it is not bundled in the TDSpeed APK or source package. Its documented UserService protocol is used here.

The Gradle wrapper is from Gradle 8.13 and distributed under Apache-2.0. AndroidX/Compose dependencies are under their respective Apache-2.0 licences. Kotlin and its standard library are from JetBrains under Apache-2.0; kotlinx.coroutines carries its upstream notices. Maven dependency metadata records the selected versions. Include the full resolved dependency notices before public release.

hidden-api-stubs contains compile-only framework signatures for callbacks. It is not shipped as Android framework code in the APK. The package audit verifies this boundary.
