"""Package the P2 pitch slice, qualifying only evidence for the exact APK."""
import hashlib
import json
import shutil
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from audit_apk import audit, elf_runtime_image

root = Path(__file__).resolve().parents[1]
version = '0.3.0-p2-pitch'
assert f'versionName = "{version}"' in (root / 'app/build.gradle.kts').read_text(encoding='utf-8')
output = root / 'artifacts/p2-pitch'
output.mkdir(parents=True, exist_ok=True)
evidence = output / 'evidence'
evidence.mkdir(exist_ok=True)
work = root / 'artifacts/p2-pitch-work'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def copy(path, name):
    assert path.is_file(), f'Missing input: {path}'
    return Path(shutil.copy2(path, output / name))

app = copy(root / 'app/build/outputs/apk/debug/app-debug.apk', f'TDSpeed-{version}-debug.apk')
test_apk = copy(root / 'app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk', f'TDSpeed-LiveTests-{version}.apk')
native_test = copy(root / 'dsp-native/build/outputs/apk/androidTest/debug/dsp-native-debug-androidTest.apk', f'TDSpeed-NativeTests-{version}.apk')
copy(root / 'dsp-native/build/outputs/aar/dsp-native-debug.aar', f'TDSpeed-NativeEngine-{version}.aar')
for name in ('P2_PITCH_STATUS.md', 'NOTICE.md', 'LICENSE'):
    copy(root / name, name)
shutil.copytree(root / 'LICENSES', output / 'LICENSES', dirs_exist_ok=True)
for entry in json.loads((root / 'ARCHITECTURE_MANIFEST.json').read_text())['files']:
    assert sha(root / entry['path']) == entry['sha256'], f'Approved architecture changed: {entry["path"]}'

audit_result = audit(app)
(output / 'APK-AUDIT.json').write_text(json.dumps(audit_result, indent=2) + '\n')
native_images = []
with zipfile.ZipFile(app) as package, zipfile.ZipFile(native_test) as test:
    for abi in ('arm64-v8a', 'x86_64'):
        path = f'lib/{abi}/libtdspeed_probe.so'
        image = elf_runtime_image(package.read(path))
        assert image == elf_runtime_image(test.read(path)), f'Native runtime mismatch: {abi}'
        native_images.append({'abi': abi, 'allocatedElfSectionsSha256': image,
                              'appAndNativeTestImageMatch': True, 'runtimeTested': abi == 'arm64-v8a'})

validation = {'version': version, 'createdAtUtc': datetime.now(timezone.utc).isoformat(),
    'apkSha256': sha(app), 'signing': 'Android debug key; experimental alpha',
    'scope': 'P2 pitch slice, not complete P2: live dry engine, RAM history, pitch, original pitch, recenter and stop',
    'nativeImages': native_images, 'staticApkAudit': 'passed',
    'wirelessAdb': 'paired and used for phone installation and tests after USB disconnected',
    'pauseResume': 'not_implemented', 'sourceTransport': 'not_implemented', 'effects': 'not_implemented',
    'storageHistory': 'not_implemented', 'bluetooth': 'not_tested', 'androidAuto': 'not_tested',
    'interruptions': 'unqualified_candidate_attenuation_only', 'soak30Minutes': 'not_tested',
    'physicalLivePitch': 'not_qualified', 'physicalRoutingCycles': 'not_qualified', 'nativeTests': 'not_qualified'}
for name, key in [('live-pitch-result.json', 'physicalLivePitch'), ('routing-cycles.json', 'physicalRoutingCycles')]:
    path = work / name
    if path.is_file():
        value = json.loads(path.read_text(encoding='utf-8-sig'))
        if value.get('result') == 'passed' and value.get('apkSha256') == sha(app):
            validation[key] = value
            shutil.copy2(path, evidence / name)
native = work / 'native-reference-result.json'
native_log = work / 'native-tests.txt'
if native.is_file() and native_log.is_file():
    data = native_log.read_bytes()
    log = data.decode('utf-16') if data.startswith((b'\xff\xfe', b'\xfe\xff')) else data.decode('utf-8-sig')
    if 'OK (7 tests)' in log and 'FAILURES!!!' not in log:
        value = json.loads(native.read_text(encoding='utf-8-sig'))
        if value.get('result') == 'passed' and 'arm64-v8a' in value.get('nativeIdentity', ''):
            validation['nativeTests'] = {'count': 7, 'result': 'passed', 'reference': value, 'testApkSha256': sha(native_test)}
            shutil.copy2(native, evidence / native.name)
for name in ('native-tests.txt', 'live-pitch-test.txt', 'routing-test.txt', 'reproducible-build.txt',
             'music-pitch.txt', 'music-listening.json', 'music-minus4-before-zero.txt', 'music-zero.txt',
             'music-recenter.txt', 'timed-pause-recenter-proof.json', 'music-final.txt'):
    path = work / name
    if path.is_file(): shutil.copy2(path, evidence / name)
music = work / 'music-listening.json'
validation['musicListening'] = json.loads(music.read_text(encoding='utf-8-sig')) if music.is_file() else 'user_listening_confirmation_pending'
for module in ('app', 'shell-audio', 'dsp-native'):
    path = root / module / 'build/reports/lint-results-debug.txt'
    assert '0 errors,' in path.read_text(encoding='utf-8')
    shutil.copy2(path, evidence / f'{module}-lint.txt')
shutil.copy2(root / 'protocol/build/test-results/testDebugUnitTest/TEST-com.fyodor.tdspeed.protocol.SessionLeaseTest.xml', evidence / 'session-unit-tests.xml')
(output / 'VALIDATION.json').write_text(json.dumps(validation, indent=2) + '\n', encoding='utf-8')

excluded = {'.gradle', '.kotlin', '.cxx', '.git', '.idea', 'build', 'artifacts', '__pycache__'}
sources = [p for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()
    and not any(x in excluded or x.startswith('build-') for x in p.relative_to(root).parts)
    and p.name != 'local.properties' and p.suffix not in ('.jks', '.keystore')]
manifest = {'version': version, 'apkSha256': sha(app), 'minSdk': 33, 'targetSdk': 36,
    'files': [{'path': p.relative_to(root).as_posix(), 'sha256': sha(p)} for p in sources]}
(output / 'BUILD-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
source_zip = output / f'TDSpeed-{version}-source.zip'
with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sources: archive.write(path, 'Android/' + path.relative_to(root).as_posix())
    archive.write(output / 'BUILD-MANIFEST.json', 'Android/BUILD-MANIFEST.json')
with zipfile.ZipFile(source_zip) as archive: assert archive.testzip() is None
(output / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(output).as_posix()}\n'
    for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS.txt'))
bundle = output.parent / f'TDSpeed-{version}.zip'
with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(output.rglob('*')):
        if path.is_file(): archive.write(path, 'TDSpeed-P2-Pitch/' + path.relative_to(output).as_posix())
with zipfile.ZipFile(bundle) as archive: assert archive.testzip() is None
print(json.dumps({'bundle': str(bundle), 'apkSha256': sha(app),
    'physicalLivePitchQualified': isinstance(validation['physicalLivePitch'], dict),
    'physicalRoutingQualified': isinstance(validation['physicalRoutingCycles'], dict),
    'nativeTestsQualified': isinstance(validation['nativeTests'], dict), 'sourceFiles': len(sources)}, indent=2))
