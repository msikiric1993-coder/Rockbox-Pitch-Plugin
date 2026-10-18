# Rockbox Pitch Plugin

A Windows listening app and VST3 inspired by Rockbox's pitch control: lower pitch slows the music down; higher pitch speeds it up. The song's length changes too.

I loved listening to slowed-down songs on my Sansa Clip Zip running Rockbox when I was younger. I couldn't find the same experience as a live effect on PC, so I made this.

**[Download the first release](https://github.com/msikiric1993-coder/Rockbox-Pitch-Plugin/releases/tag/v0.4.1)** | Windows 11 x64

![Plugin interface](docs/images/interface.png)

## How it works

Incoming audio is buffered and played back at the speed selected by the pitch slider. Pitch and tempo change together.

The standalone app routes sound through a virtual audio cable, processes it and sends it to your speakers or headphones. That makes it a live effect for **YouTube, Spotify, Tidal** and other apps routed through the cable.

## The trade-off: growing delay

When you slow live audio, new audio arrives faster than it plays. Unplayed audio accumulates, so the delay keeps growing. If you change songs, you will still hear the queued audio first.

Press **Re-centre buffer** to discard that backlog and return near the current audio. History uses up to **2 GiB of RAM**; at its limit, playback pauses for three seconds, resets the buffer and continues.

That delay is unavoidable when slowing a live source without discarding audio. For me, the sound is worth it.

## Effects

Reverb can really enhance slowed-down music. Open **Effects** for **three independent slots** with reverb, delay, chorus and more, then save combinations under **Saved racks**. Older settings can also retain a fourth, legacy reverb such as PSX. **Mix** is dry at 0% and fully processed at 100%.

## Install and listen

1. Download and run **TDSpeed-Setup-0.4.1.exe**. It reuses a supported existing cable or installs the included standard VB-CABLE when needed. Restart Windows if requested.
2. Open **TDSpeedPreview**. In **Audio settings**, select the cable and your physical listening output, such as your DAC, audio interface or headphones.
3. Play some music, press **Enable processing** and move the pitch slider.

Already have a usable **Hi-Fi Cable or standard VB-CABLE**? Extract the **Portable ZIP** and run `TDSpeedPreview.exe`. No VSTHost or ASIO needed.

Before Enable, audio passes through unchanged. Closing hides the app to the tray; **Exit** restores normal Windows audio. The app and installer are unsigned.

## VST3

Extract the **VST3 ZIP**, copy the complete `TDSpeedPreview.vst3` bundle into your DAW's VST3 folder and rescan. The plugin does **not** need a virtual cable.

To keep multiple processed tracks synchronized:

1. Select **Follow DAW** in each instance, with playback stopped.
2. Click **Create group** on one instance. It becomes the group leader. Use **Join group** on the others.
3. Set pitch on the leader and press Play. All members share pitch and bypass, while their effects and output levels remain independent.

Pitch changes during playback apply after **Stop/Play**. Grouped tracks share their buffered timing; they cannot stay aligned indefinitely with untreated tracks or different speeds.

## Source and credits

[Build instructions](docs/BUILD.md) | [Effect guide](docs/EFFECTS.md) | [Troubleshooting](docs/TROUBLESHOOTING.md)

[AGPL-3.0](LICENSE), with [third-party engine licences](Source/ThirdParty/VERSIONS.md). An independent Rockbox-inspired project, not an official release or bit-exact port.

The bundled standard **VB-CABLE** is donationware by [VB-Audio](https://vb-cable.com). [Donations and licences](https://vb-audio.com/Services/licensing.htm) support its developer.
