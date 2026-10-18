# Rockbox Pitch Plugin — TDSpeed 0.4.1

Portable Windows listening app and separate VST3. This version adds synchronized DAW playback and whole-interface scaling. Version 0.4.0 is preserved. [Download the installer, portable application, VST3 and source](https://github.com/msikiric1993-coder/Rockbox-Pitch-Plugin/releases/tag/v0.4.1).

## Two DAW tracks

Load TDSpeedPreview on each track. Select Follow DAW on both. With playback stopped, press Create group on the first instance; select that group in Join group on the second. Set pitch on the leader. Followers share the same speed, while their effects and output trims remain independent. Press Play: both start fresh at the DAW cursor. Stop freezes history; Play, seeks and loop jumps start a fresh epoch. Linked pitch changes during playback remain pending until Stop/Play. Use the plugin's Bypass group control to switch both tracks to dry; internal synchronized processing continues behind bypass.

Groups operate inside one host process, with up to 32 members. They do not link separate bridged processes. All members must process the same transport run. Late or suspended instances show Restart playback to synchronize rather than invent missing audio. Hosts that completely skip a bypassed plugin can cause this warning when it returns; use Bypass group inside the plugin. Process linked tracks together for offline rendering. A single plugin on their common bus remains an alternative.

Rockbox-style pitch also changes speed. Processed tracks using one speed can align with each other; they cannot stay aligned indefinitely with untreated tracks or different speeds. Growing backlog is intentional and cannot be removed by fixed DAW delay compensation. Hermite interpolation, 2 GiB history and the three-second limit recovery remain.

## Listening app

Install with TDSpeed-Setup-0.4.1.exe, or extract the portable ZIP and run TDSpeedPreview.exe. It always uses Listening mode and retains automatic dry forwarding, held playback through Disable/Enable, effects and tray recovery. Existing cables are reused. Preview preferences, racks, startup and recovery files are separate; copies are imported from TDSpeed RC or the older app. Closing hides to tray; Exit restores owned Windows routes. Startup registration defaults off.

## Window and controls

Choose Auto or 50â€“200% in 25% steps. Auto uses DPI-aware monitor space; manual percentages are retained, temporarily reduced to fit expanded controls and restored when space is available. Effects and Advanced expand/shrink automatically. Controls stay in one fixed layout. The plugin requests fixed content dimensions using VST3 sizing; a host-imposed viewport scales the entire interface without clipping. The DAW owns its outer frame.

Mix 0% is dry and 100% is fully processed. Wet Level, Slot Output and Final Output retain their calibrated 0.4.0 behavior. Saved racks affect effects/output only, not timing mode or group membership. Save/Load/overwrite are retained; A/B remains removed.

## Build and source

The complete working source and vendored dependencies are included in this repository and the corresponding-source ZIP. Use Visual Studio 2022 x64, CMake and the windows-release preset. Build TDSpeedApp, TDSpeedVST_VST3 and tests. Tools/Build-Standalone.ps1 builds, tests and packages the installer, portable application, VST3 and source. See [build instructions](docs/BUILD.md). Read [validation notes](docs/VALIDATION.md) and the release’s Validation-report.txt for passed checks and any remaining host-specific limitations.

## Installation

Windows 11 x64. The app and plugin remain named TDSpeedPreview to preserve 0.4.1 sessions and preferences. Older TDSpeed RC installations are separate. Install only one copy of TDSpeedPreview.vst3 in your DAW scan paths; remove a manually copied preview before installing the same bundle elsewhere. The standalone installer does not install the VST3 automatically: extract the separate VST3 ZIP and copy its complete bundle to your VST3 folder.

The listening app needs a virtual audio cable; the VST3 does not. Setup detects existing usable Hi-Fi Cable or standard VB-CABLE and only offers the original signed standard VB-CABLE when none is installed. VB-CABLE is donationware by [VB-Audio](https://vb-cable.com); [donations and licences](https://vb-audio.com/Services/licensing.htm) support its developer. TDSpeed itself and its installer are unsigned.

![TDSpeed interface](docs/images/interface.png)

## Licence and credits

TDSpeed is distributed under AGPL-3.0; dependencies retain their respective licences. See [dependency revisions and adaptations](Source/ThirdParty/VERSIONS.md), the root LICENSE and packaged Licences directory. This is a Rockbox-inspired coupled pitch/speed effect, not an official Rockbox product or a bit-exact port of its audio pipeline.
