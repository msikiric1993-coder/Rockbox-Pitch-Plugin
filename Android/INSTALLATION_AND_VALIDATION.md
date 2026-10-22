# TDSpeed Android installation and validation

This is the installation contract for the future APK. No APK is included in this architecture baseline. The initial profile requires an unrooted Android 13+ ARM64 phone and Shizuku started as ADB shell.

## First installation

1. Record the phone model, Android/API version, One UI version and build fingerprint. For the first device, record the S24 Ultra's actual installed software.
2. Install Shizuku using its [official distribution instructions](https://shizuku.rikka.app/download/).
3. Enable Developer options and start Shizuku through wireless debugging, following its [setup guide](https://shizuku.rikka.app/guide/setup/). A computer-assisted ADB start is an alternative when wireless setup is unavailable. Pairing and starting the service are separate steps.
4. Install the signed TDSpeed APK. Grant Shizuku access when TDSpeed requests it. A normal runtime permission dialog cannot grant the APK MODIFY_AUDIO_ROUTING.
5. Grant notification access only if choosing the optional source-control fallback; grant notification-display permission for visible session controls where requested. The base path must explain that it does not capture the microphone.
6. Run the routing check with the debug source or explicit listening test before enabling a long history. Confirm stereo, suppression of original playback and successful stop.
7. Select the history capacity and run a chosen music app. Start near live, adjust pitch and test pause/re-centre.

Samsung Auto Blocker can block outside-store installation and USB commands. Detect blocked steps and link to the relevant setting rather than attempting to disable security features programmatically. Ask the user to change only the setting necessary for the chosen setup path. Do not claim that re-enabling every restriction is compatible with Shizuku; re-run the backend check after a security-setting change. [Samsung's description](https://www.samsung.com/ca/support/mobile-devices/protect-your-samsung-galaxy-with-auto-blocker/).

Keep Shizuku and TDSpeed out of Samsung's sleeping-app restrictions if tests show that the phone suspends them. Describe the exact device setting used in the compatibility result. Avoid blanket battery-setting changes without evidence that they are needed.

## Subsequent use and reboot

During an ordinary session, start TDSpeed and play music in the selected app. The running helper can be controlled through the notification or car interface. Closing the phone UI leaves the explicitly started session active.

After reboot, Shizuku must be started again under the unrooted setup. TDSpeed must report this plainly and remain stopped until the backend and permissions are ready. Do not promise unattended service restart or start mediaPlayback directly from BOOT_COMPLETED. [Shizuku lifecycle](https://shizuku.rikka.app/guide/setup/), [foreground-service rules](https://developer.android.com/develop/background-work/services/fgs/service-types#media).

An APK cannot silently enable debugging, approve Shizuku or bypass Samsung's installer checks. The package should provide a guided setup with a capability result after each step.

## Android Auto installation

The phone APK declares media support and exposes the MediaLibraryService. Android Auto displays its own media interface and forwards commands to the phone. No Android package is installed into the car for this initial design.

Use Desktop Head Unit for development. For a personal sideloaded media APK in the actual car, Android Auto's own developer option for unknown sources may be required. It is separate from the phone's permission to install an APK. A trusted test distribution is another path. These are documented testing mechanisms, not a promise of public store acceptance. [Android Auto manifest support](https://developer.android.com/training/cars/media/auto), [testing and unknown sources](https://developer.android.com/training/cars/testing).

Test audible output and launcher discovery separately. A TDSpeed icon is not evidence that audio is processed; processed audio is not evidence that dashboard controls work. Configure onboarding and detailed effects while parked.

## Update and removal

An update uses the same application ID and signing identity. Stop an active session before replacing helper code. On next launch, invalidate old helper/ABI versions, remove orphan history and retain compatible user settings. Migrate settings explicitly when their schema changes.

Stop processing before uninstalling. Uninstall removes the application and private history; it does not uninstall Shizuku or revert user-chosen debugging settings. Shizuku can remain for other apps. Verify the normal music route after removal. Test removal during a running session as a failure scenario too, because users may not follow the preferred sequence.

## Compatibility matrix

Create one result per device firmware, source version/mode and output route. Start from [compatibility-template.json](compatibility-template.json); never turn not_tested into passed because a nearby combination passed.

| Dimension | Required coverage |
|---|---|
| Device | S24 Ultra first; at least one AOSP-like Android 13+ device for comparison when available |
| OS | Actual S24 firmware first; API 33 minimum and current target APIs through emulators/devices |
| Sources | YouTube, YouTube Music, Tidal, Spotify, a local player, browser media and deterministic debug source |
| Playback modes | Streaming; downloaded/offline where supported; source quality/offload options recorded |
| Output | Phone speaker, wired/USB output when available, Bluetooth, wired Android Auto, wireless Android Auto |
| Coexistence | Navigation, assistant, incoming/outgoing calls, alarms and notifications |
| Lifecycle | Screen lock, Doze, battery saver, car reconnect, source restart, app/helper death, Shizuku restart, reboot |

Use source app display names and resolved package/version data from the device. Do not hard-code a guessed Tidal or YouTube package into release logic.

## Required acceptance scenarios

**Routing:** use distinct stereo test tones. Compare direct output with redirected output and a known diagnostic gain change. Confirm that only one audible path remains, capture excludes the helper, and stopping restores normal routing. Include silence to distinguish idle status from a capture failure. Test two app UIDs to establish filtering semantics.

**Fidelity:** use a 440 Hz tone and known transients. At minus 12 semitones, expect approximately 220 Hz with doubled programme duration; at minus 4, approximately 349.23 Hz. Check samples against the reference fixtures and verify long-run source-cursor advance. Music listening complements these measurements; it does not replace them.

**History:** run beyond the old small-buffer boundary, across RAM/disk page boundaries and physical file wrap. Check monotonically ordered source markers. Trigger capacity limits with small test budgets and verify three seconds of engine silence followed by a fresh near-live epoch. Manual re-centre must retain pitch and effect parameters.

**Pause:** press pause with audio queued to the car, then resume. Verify the presented-position model and record any route-dependent error. A failed upstream pause must be visible while local listening remains held. A resumed session must not silently re-centre.

**Interruptions:** test calls and navigation with several minutes of music backlog. Guidance must sound promptly and normal speed. Buffered music must mute/duck promptly rather than waiting for a gain change already captured far ahead. Confirm no double ducking and no delayed replay of the prompt. Unknown interruption behaviour blocks the car-support claim.

**Disconnect:** unplug wired Auto or drop wireless/Bluetooth while held and while playing. Hold output immediately and prevent unexpected phone-speaker playback. Require an explicit resume unless the reconnection policy has been separately tested and enabled. Route changes must retain history when the engine format remains valid; any necessary discard must be reported.

**Recovery:** terminate the Activity, foreground process, helper and Shizuku separately. Inject native-load, policy, file and output errors. Confirm watchdog progress, route cleanup and fresh-start behaviour. The emergency sequence is Stop in TDSpeed, stop Shizuku if needed, restart the source, and reboot only if framework routing remains stuck. Record any case needing reboot as a failed recovery test.

**Long sessions:** perform the milestone soak runs with screen off and representative car load. Record peak RSS, queue/callback timing, xruns, file writes, temperature and battery change. Record charging and navigation state because they materially affect car measurements.

## Evidence and release wording

Export a structured report using the template, with measured counters and explicit results. Keep synthetic audio fixtures separate from ordinary diagnostic logs. Record timestamps, APK hash, source revision, device/OS, app versions, negotiated formats, route and test duration.

Use passed, failed, blocked or not_tested. blocked identifies a prerequisite failure; it is not a pass. Report phone audio processing and Android Auto controls independently. Release claims name the tested combinations. Do not label silence as DRM, or label all downloaded music unsupported, without evidence identifying that cause.
