"""Stage only fixtures from a successfully completed Windows reference comparison."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
build = root / 'dsp-native/host/build'
fixture = build / 'reference-fixtures.bin'
log = (build / 'Testing/Temporary/LastTest.log').read_text(encoding='utf-8')
assert 'PASS cases=24 comparedStereoSamples=26334000 maxAbsError=0 tolerance=1e-6' in log
assert 'WindowsReferenceComparison' in log and 'Test Passed.' in log
assets = root / 'dsp-native/src/androidTest/assets'
assets.mkdir(parents=True, exist_ok=True)
shutil.copy2(fixture, assets / 'p1-reference-fixtures.bin')
provenance = {
    'format': 'TDSP1F01 little endian',
    'fixtureSha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
    'oracle': 'Actual Windows 0.4.2-rc.1 shared-code library, dry listening mode',
    'desktopComparison': {'cases': 24, 'stereoSamplesCompared': 26334000, 'maximumAbsoluteError': 0, 'tolerance': 1e-6},
    'signals': ['440/659 Hz stereo tones', 'stereo ramp', 'deterministic stereo noise', 'offset channel impulses'],
    'scenarios': ['unity', '-12', '-4', '+4', 'pitch changes', 'recenter then zero return', 'history full', 'caught live input'],
    'sampleRates': [44100, 48000, 96000],
    'windowsInputs': json.loads((build / 'reference-inputs.json').read_text(encoding='utf-8')),
    'comparisonHarnessSha256': hashlib.sha256((root / 'dsp-native/host/ReferenceComparison.cpp').read_bytes()).hexdigest(),
    'coreSha256': hashlib.sha256((root / 'dsp-native/src/main/cpp/core/VarispeedEngine.cpp').read_bytes()).hexdigest(),
    'androidScope': 'Three stereo checkpoint frames and recovery/backlog state per variable-sized block. Desktop compares every sample.',
    'arm64Runtime': 'not_tested',
}
(assets / 'p1-reference-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
print(f'Staged {fixture.stat().st_size} bytes of actual reference fixtures: {provenance["fixtureSha256"]}')
