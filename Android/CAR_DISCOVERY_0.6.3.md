# Car Lab 0.6.3 — connection identity survives Start/Stop

## Field evidence

The user confirms audible coupled pitch/speed in the real car, live slider
response, and initially successful Stop/Start. The later loss of the effect
followed possible use of the ordinary app. This is a successful real-car use
observation with a subsequent reproducible discovery defect, not a claim that
all Android Auto scenarios are qualified.

Both JSON reports were exported after stopping; their audio fields retain the
last session's measurements. At 14:38, the last session had nonzero input/output,
-9 semitones, and REROUTED. At 14:42, the last session had zero input/output,
-4 semitones, and WAITING_FOR_ANDROID_AUTO. Both reports still identify the
same Android Auto output registration, 2068906430:ap:238.

The first report's ten-entry dynamic policy event history includes Android
Auto's 14:34:56 registration. The second history consists of newer shell-helper
Start/Stop events and no longer contains that registration. Car Lab 0.6.2 derived
ownership from this bounded history on every guardian launch and refresh. It
therefore forgot an existing car route. This explains the failure status while
the car remained connected. Starting the ordinary app can accelerate eviction;
it is not required to trigger the defect and is not proven to have damaged the
car connection. Repeated Car Lab sessions alone are sufficient.

Original reports stay under the local artifact directory's private-reports
folder, outside the repository. Do not commit/upload those files.

## Fix

- Preserve verified addresses in a shell-private AtomicFile across guardian
  processes. Bind the registry to kernel boot UUID, build fingerprint, app
  package and both controller/projection UIDs. Files use mode 0600. Invalid
  entries are ignored. Without a readable boot UUID, persistence is disabled.
- Reuse an identity only while its exact native mix still exists. Drop removed
  addresses rather than guessing from a similar-looking mix. Native type,
  device, flags and media/game rule checks remain in force before a new edit.
- Learn ownership from a currently active Android Auto recording configuration
  when UID, package and device address are available. This can bootstrap an
  already-connected car even after the registration event has disappeared.
- Keep registration-log discovery for new connections; retain its verified
  result through later log rollover and Start/Stop.
- Report carRouteDiscovery and bootId. Mark audioMetricsFromStoppedSession so
  stale last-session measurements are not confused with active processing.
- Refuse Car Lab startup with an explicit OTHER_VARISPEED_LISTENING error if
  the ordinary app's exact input-policy rules are already active. It does not
  force-stop the ordinary app or replace its installation.

No DSP, 1 GiB audio history, pitch controls, ordinary app installation, navigation
mix or audio saving behavior is changed. This remains the separate Car Lab app.

## Regression

ProjectionDiscoveryTest replays the relevant identifiers and ownership events
from the two reports: the older car registration, then a history containing
only newer shell registrations while the same native car mix remains alive.
It checks retention across a new discovery instance, recorder fallback,
disconnection, wrong owner UID and misleading registration prefixes.

The shell policy suite adds a real two-process regression: seed verified
ownership into the registry, stop the first guardian and verify restoration,
launch a second guardian with a rolled-over ownership history, recover through
the persisted identity, forward transformed audio, then verify restoration again.
This uses a quiet synthetic source and a simulated car receiver, not the native
varispeed DSP or an actual head unit.

The complete suite now has thirteen routing cases and fifteen protocol unit
tests. Build, app lint and helper lint are also checked. Results and APK belong
in artifacts/car-discovery-0.6.3. The ordinary 0.5.3 app remains installed separately.

## First use and limits

The old 0.6.2 installation did not retain connection identities. If installing
0.6.3 into an existing connection whose registration has already disappeared,
recovery depends on Android exposing the active recorder metadata. Otherwise a
fresh Android Auto connection is needed once to establish ownership. Subsequent
Start/Stop sessions reuse that verified connection without relying on the log.

Physical validation of 0.6.3 still requires the user's phone and car. The prior
real-car success belongs to 0.6.2; the regression fix must not be described as
already verified in the car.
