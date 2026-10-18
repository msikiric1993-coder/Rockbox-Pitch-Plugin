# 0.4.2 export-tail preview

0.4.1 reports effect decay but omits buffered pitch audio. This preview adds remaining signal-history duration, converted from source frames to output time at the active speed. Incoming silence drains this estimate instead of extending it forever. Processing, pitch phase, synchronization, effects and held Listening bypass are unchanged.

VST3 cannot resize a host clip or override an explicitly selected export endpoint. This metadata improvement only helps hosts that query and honor the updated tail while processing continues. It is not yet a verified automatic export fix for Sonar or Audacity. Keep the longer-time-selection workaround.

Finite-source regression checks cover a marker at the clip ending, an extended render, reported tail duration and final drainage at 44.1/48/96 kHz and -12/-3/0 semitones.

## Local results

Four targeted Release tests passed in 5.29 seconds, including the actual VST3 end-marker/tail query. Nine finite-source rate/pitch combinations passed. Twelve deterministic renders are byte-identical to 0.4.1. No native Sonar/Audacity export acceptance or new sanitizer pass is claimed; keep the workaround and the stable release known issue open.
