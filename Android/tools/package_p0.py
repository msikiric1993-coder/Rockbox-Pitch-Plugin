"""Package P0 without SDK caches, signing keys, downloaded Shizuku, or unqualified pass claims."""
import hashlib
import json
import re
import shutil
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from audit_apk import audit

root = Path(__file__).resolve().parent.parent
if 'versionName = "0.1.0-p0"' not in (root / 'app/build.gradle.kts').read_text(encoding='utf-8'):
    raise SystemExit('P0 is archived. Use package_p1.py for the current source; do not overwrite the P0 baseline.')
output = root / 'artifacts' / 'p0'
output.mkdir(parents=True, exist_ok=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy(relative, name=None):
    source = root / relative
    target = output / (name or source.name)
    shutil.copy2(source, target)
    return target


app = copy('app/build/outputs/apk/debug/app-debug.apk', 'TDSpeed-RoutingLab-0.1.0-p0-debug.apk')
copy('test-source/build/outputs/apk/debug/test-source-debug.apk', 'TDSpeed-StereoSource-0.1.0-p0-debug.apk')
copy('P0_STATUS.md')
copy('NOTICE.md')
copy('LICENSE')
shutil.copytree(root / 'LICENSES', output / 'LICENSES', dirs_exist_ok=True)
package_audit = audit(app)
(output / 'APK-AUDIT.json').write_text(json.dumps(package_audit, indent=2) + '\n', encoding='utf-8')

evidence = output / 'evidence'
evidence.mkdir(exist_ok=True)
for name in ('final-routing-instrumentation.txt', 'p0-routing-cycles.json', 'p0-active-audioflinger.txt',
             'p0-controller-death-audioflinger.txt', 'p0-live-service.txt', 'p0-running.png'):
    source = root / 'artifacts' / name
    if source.is_file():
        shutil.copy2(source, evidence / name)
for relative, name in (
    ('protocol/build/test-results/testDebugUnitTest/TEST-com.fyodor.tdspeed.protocol.SessionLeaseTest.xml', 'session-unit-tests.xml'),
    ('app/build/reports/lint-results-debug.txt', 'app-lint.txt'),
    ('test-source/build/reports/lint-results-debug.txt', 'test-source-lint.txt'),
):
    source = root / relative
    if source.is_file(): shutil.copy2(source, evidence / name)
native_reports = list((root / 'dsp-native/build/outputs/androidTest-results/connected/debug').glob('*.xml'))
for i, source in enumerate(native_reports): shutil.copy2(source, evidence / f'native-tests-{i}.xml')

# Runtime evidence must identify the exact APK. Merely finding an old passing file is insufficient.
runtime = None
record = root / 'artifacts/p0-routing-cycles.json'
if record.is_file():
    candidate = json.loads(record.read_text(encoding='utf-8-sig'))
    if candidate.get('apkSha256') == sha(app) and candidate.get('result') == 'passed':
        runtime = candidate
validation = {
    'packagedApkSha256': sha(app), 'staticApkAudit': 'passed',
    'runtimeEvidenceMatchesApk': runtime is not None,
    'emulatorRoutingCycles': len(runtime['cycles']) if runtime else None,
    'controllerLeaseExpiry': 'passed' if runtime and runtime.get('controllerLeaseExpiryPassed') else 'not_qualified',
    'physicalS24Ultra': 'not_tested', 'commercialMusicApps': 'not_tested',
    'androidAuto': 'not_tested', 'pageSize16KiBRuntime': 'not_tested',
    'milestoneP0': 'partial_device_qualification_remaining',
}
(output / 'VALIDATION.json').write_text(json.dumps(validation, indent=2) + '\n', encoding='utf-8')

excluded = {'.gradle', '.kotlin', '.cxx', '.git', '.idea', 'build', 'artifacts', '__pycache__'}
sources = []
for path in sorted(root.rglob('*')):
    rel = path.relative_to(root)
    if not path.is_file() or path.is_symlink() or any(part in excluded for part in rel.parts): continue
    if path.name == 'local.properties' or path.suffix in ('.jks', '.keystore'): continue
    sources.append(path)
manifest = {
    'package': 'TDSpeed Android P0', 'version': '0.1.0-p0',
    'createdAtUtc': datetime.now(timezone.utc).isoformat(),
    'signing': 'Android debug key; not a production release',
    'minSdk': 33, 'targetSdk': 36, 'abis': ['arm64-v8a', 'x86_64'],
    'toolchain': {'gradle': '8.13', 'androidGradlePlugin': '8.13.2', 'kotlin': '2.2.21',
                  'ndk': '28.2.13676358', 'cmake': '3.22.1'},
    'files': [{'path': str(p.relative_to(root)).replace('\\', '/'), 'sha256': sha(p)} for p in sources],
}
(output / 'BUILD-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
with zipfile.ZipFile(output / 'TDSpeed-Android-0.1.0-p0-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sources: archive.write(path, 'Android/' + path.relative_to(root).as_posix())
    archive.write(output / 'BUILD-MANIFEST.json', 'Android/BUILD-MANIFEST.json')
with zipfile.ZipFile(output / 'TDSpeed-Android-0.1.0-p0-source.zip') as archive:
    assert archive.testzip() is None
(output / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(output).as_posix()}\n'
    for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS.txt'), encoding='utf-8')
bundle = output.parent / 'TDSpeed-Android-0.1.0-p0.zip'
with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(output.rglob('*')):
        if path.is_file(): archive.write(path, 'TDSpeed-Android-P0/' + path.relative_to(output).as_posix())
with zipfile.ZipFile(bundle) as archive:
    assert archive.testzip() is None
print(json.dumps({'output': str(output), 'bundle': str(bundle), 'sourceFiles': len(sources), 'validation': validation}, indent=2))
