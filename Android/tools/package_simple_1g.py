"""Package the simple RAM edition with evidence tied to its installed APK."""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path
from audit_apk import audit, elf_runtime_image

root = Path(__file__).resolve().parents[1]
version = '0.4.0-simple-1g'
assert f'versionName = "{version}"' in (root / 'app/build.gradle.kts').read_text()
out = root / 'artifacts/simple-1g'
out.mkdir(parents=True, exist_ok=True)
work = root / 'artifacts/simple-1g-work'
evidence = out / 'evidence'
evidence.mkdir(exist_ok=True)
def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):
    return json.loads(p.read_text(encoding='utf-8-sig'))
def log(p):
    data = p.read_bytes()
    return data.decode('utf-16') if data.startswith((b'\xff\xfe', b'\xfe\xff')) else data.decode('utf-8-sig')
app = Path(shutil.copy2(root / 'app/build/outputs/apk/debug/app-debug.apk', out / f'TDSpeed-{version}.apk'))
native = root / 'dsp-native/build/outputs/apk/androidTest/debug/dsp-native-debug-androidTest.apk'
assert 'OK (8 tests)' in log(work / 'native-tests.txt')
assert 'OK (1 test)' in log(work / 'live-tests.txt')
assert 'OK (1 test)' in log(work / 'routing-tests.txt')
allocation = read(work / 'one-gib-allocation.json')
assert allocation['result'] == 'passed' and allocation['historyBytes'] == 1073741824
for name in ('live-pitch-result.json', 'routing-cycles.json'):
    value = read(work / name)
    assert value['result'] == 'passed' and value['apkSha256'] == sha(app)
for entry in read(root / 'ARCHITECTURE_MANIFEST.json')['files']:
    assert sha(root / entry['path']) == entry['sha256']
with zipfile.ZipFile(app) as a, zipfile.ZipFile(native) as n:
    for abi in ('arm64-v8a', 'x86_64'):
        path = f'lib/{abi}/libtdspeed_probe.so'
        assert elf_runtime_image(a.read(path)) == elf_runtime_image(n.read(path))
for name in ('SIMPLE_1G_STATUS.md', 'NOTICE.md', 'LICENSE'):
    shutil.copy2(root / name, out / name)
shutil.copytree(root / 'LICENSES', out / 'LICENSES', dirs_exist_ok=True)
for p in work.iterdir():
    if p.is_file() and p.suffix in ('.json', '.txt'):
        shutil.copy2(p, evidence / p.name)
for module in ('app', 'shell-audio', 'dsp-native'):
    p = root / module / 'build/reports/lint-results-debug.txt'
    assert '0 errors,' in p.read_text()
    shutil.copy2(p, evidence / f'{module}-lint.txt')
shutil.copy2(root / 'protocol/build/test-results/testDebugUnitTest/TEST-com.fyodor.tdspeed.protocol.SessionLeaseTest.xml', evidence / 'unit-tests.xml')
validation = {'version': version, 'apkSha256': sha(app), 'audit': audit(app),
    'nativeTests': 8, 'nativeTestApkSha256': sha(native), 'oneGiBAllocation': allocation,
    'livePitch': read(work / 'live-pitch-result.json'),
    'routingCycles': read(work / 'routing-cycles.json'),
    'bluetooth': 'not_tested', 'androidAuto': 'not_tested', 'longSoak': 'not_tested',
    'signing': 'development debug key', 'audioStorage': 'RAM only; no audio files'}
(out / 'VALIDATION.json').write_text(json.dumps(validation, indent=2) + '\n')
excluded = {'.gradle', '.kotlin', '.cxx', '.git', '.idea', 'build', 'artifacts', '__pycache__'}
sources = [p for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()
    and not any(x in excluded or x.startswith('build-') for x in p.relative_to(root).parts)
    and p.name != 'local.properties' and p.suffix not in ('.jks', '.keystore')]
with zipfile.ZipFile(out / f'TDSpeed-{version}-source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sources:
        z.write(p, 'Android/' + p.relative_to(root).as_posix())
(out / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(out).as_posix()}\n'
    for p in sorted(out.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS.txt'))
bundle = out.parent / f'TDSpeed-{version}.zip'
with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(out.rglob('*')):
        if p.is_file(): z.write(p, 'TDSpeed/' + p.relative_to(out).as_posix())
with zipfile.ZipFile(bundle) as z: assert z.testzip() is None
print(json.dumps({'bundle': str(bundle), 'apkSha256': sha(app), 'tests': 'passed'}, indent=2))
