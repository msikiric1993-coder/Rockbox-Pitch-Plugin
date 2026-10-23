"""Package only the separate Android Auto experiment and exact passing evidence."""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path
from audit_apk import audit

root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/car-lab'
out.mkdir(parents=True, exist_ok=True)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def readlog(p):
    b = p.read_bytes()
    return b.decode('utf-16') if b.startswith((b'\xff\xfe', b'\xfe\xff')) else b.decode('utf-8-sig')

app = Path(shutil.copy2(root / 'app/build/outputs/apk/carLab/app-carLab.apk', out / 'Varispeed-Car-Lab-0.6.0.apk'))
audit_result = audit(app)
assert 'BUILD SUCCESSFUL' in readlog(root / 'artifacts/car-lab-build.txt')
assert 'OK (3 tests)' in readlog(root / 'artifacts/car-lab-ui-tests.txt')
chain_text = readlog(root / 'artifacts/car-policy-chain-tests.txt')
receipt = json.loads(next(line.split('POLICY_CHAIN_RESULT=', 1)[1] for line in chain_text.splitlines() if line.startswith('POLICY_CHAIN_RESULT=')))
assert receipt['result'] == 'passed' and len(receipt['cases']) == 6
assert all(c['result'] == 'passed' and c['policiesUnregistered'] and not c['cleanupErrors'] for c in receipt['cases'])
assert receipt['routeReportBinderBytes'] < 512 * 1024
baseline = root.parent / 'Repository/Android/releases/0.5.3-responsive-slider/Varispeed-0.5.3.apk'
with zipfile.ZipFile(app) as a, zipfile.ZipFile(baseline) as b:
    for abi in ('arm64-v8a', 'x86_64'):
        name = f'lib/{abi}/libtdspeed_probe.so'
        assert a.read(name) == b.read(name)
for entry in json.loads((root / 'ARCHITECTURE_MANIFEST.json').read_text())['files']:
    assert sha(root / entry['path']) == entry['sha256']
for n in ('car-lab-build.txt', 'car-lab-ui-tests.txt', 'car-policy-chain-tests.txt', 'car-lab-ui.png'):
    shutil.copy2(root / 'artifacts' / n, out / n)
for n in ('CAR_LAB_STATUS.md', 'NOTICE.md', 'LICENSE'):
    shutil.copy2(root / n, out / n)
shutil.copytree(root / 'LICENSES', out / 'LICENSES', dirs_exist_ok=True)
for module, variant in (('app', 'carLab'), ('shell-audio', 'debug')):
    p = root / module / f'build/reports/lint-results-{variant}.txt'
    assert '0 errors,' in p.read_text(encoding='utf-8')
    shutil.copy2(p, out / f'{module}-lint.txt')
shutil.copy2(root / 'protocol/build/test-results/testDebugUnitTest/TEST-com.fyodor.tdspeed.protocol.SessionLeaseTest.xml', out / 'protocol-unit-tests.xml')
validation = {'version': '0.6.0-car-lab', 'applicationId': 'com.fyodor.tdspeed.android.carlab',
    'apkSha256': sha(app), 'apkAudit': audit_result, 'nativeLibrariesMatchSuccessfulBaseline': True,
    'policyChain': receipt, 'uiTests': 3, 'normalPhoneAppReplaced': False,
    'realAndroidAuto': 'NOT_TESTED', 'physicalPhone': 'NOT_TESTED',
    'scope': 'Synthetic routing mechanism validated; not proof of Samsung/Android Auto compatibility',
    'signing': 'development debug key'}
(out / 'VALIDATION.json').write_text(json.dumps(validation, indent=2) + '\n')
excluded = {'.gradle', '.kotlin', '.cxx', '.git', '.idea', 'build', 'artifacts', 'releases', '__pycache__', 'Repository'}
sources = [p for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()
    and not any(x in excluded or x.startswith('build-') for x in p.relative_to(root).parts)
    and p.name != 'local.properties' and p.suffix not in ('.jks', '.keystore')]
with zipfile.ZipFile(out / 'Varispeed-Car-Lab-source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sources: z.write(p, 'Android/' + p.relative_to(root).as_posix())
(out / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(out).as_posix()}\n'
    for p in sorted(out.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS.txt'))
bundle = out.parent / 'Varispeed-Car-Lab-0.6.0.zip'
with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(out.rglob('*')):
        if p.is_file(): z.write(p, 'Varispeed-Car-Lab/' + p.relative_to(out).as_posix())
with zipfile.ZipFile(bundle) as z: assert z.testzip() is None
print(json.dumps({'apk': str(app), 'sha256': sha(app), 'routingTests': 6, 'uiTests': 3, 'realCar': 'not tested'}, indent=2))
