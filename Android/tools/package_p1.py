"""Package the dry-core milestone with byte-matched evidence, preserving P0."""
import hashlib
import json
import re
import shutil
import xml.etree.ElementTree as ET
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from audit_apk import audit, elf_runtime_image

root = Path(__file__).resolve().parents[1]
version = '0.2.0-p1-core'
assert f'versionName = "{version}"' in (root / 'app/build.gradle.kts').read_text(encoding='utf-8')
output = root / 'artifacts/p1'
output.mkdir(parents=True, exist_ok=True)
evidence = output / 'evidence'
evidence.mkdir(exist_ok=True)
recheck = root / 'artifacts/recheck-2026-10-21'
host = root / 'dsp-native/host/build'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def copy(source, destination):
    assert source.is_file(), f'Missing required input: {source}'
    shutil.copy2(source, destination)
    return destination

baseline = json.loads((root / 'ARCHITECTURE_MANIFEST.json').read_text(encoding='utf-8'))
for entry in baseline['files']:
    assert sha(root / entry['path']) == entry['sha256'], f'Approved architecture changed: {entry["path"]}'
provenance_file = root / 'dsp-native/src/androidTest/assets/p1-reference-provenance.json'
provenance = json.loads(provenance_file.read_text(encoding='utf-8'))
assert sha(root / 'dsp-native/src/main/cpp/core/VarispeedEngine.cpp') == provenance['coreSha256']
assert sha(root / 'dsp-native/host/ReferenceComparison.cpp') == provenance['comparisonHarnessSha256']
assert sha(root / 'dsp-native/src/androidTest/assets/p1-reference-fixtures.bin') == provenance['fixtureSha256']
assert 'PASS cases=24 comparedStereoSamples=26334000 maxAbsError=0 tolerance=1e-6' in (host / 'Testing/Temporary/LastTest.log').read_text(encoding='utf-8')

app = copy(root / 'app/build/outputs/apk/debug/app-debug.apk', output / f'TDSpeed-RoutingLab-{version}-debug.apk')
native_test = copy(root / 'dsp-native/build/outputs/apk/androidTest/debug/dsp-native-debug-androidTest.apk', output / f'TDSpeed-NativeTests-{version}.apk')
copy(root / 'app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk', output / f'TDSpeed-RoutingTests-{version}.apk')
aar = copy(root / 'dsp-native/build/outputs/aar/dsp-native-debug.aar', output / f'TDSpeed-NativeEngine-{version}.aar')
copy(root / 'test-source/build/outputs/apk/debug/test-source-debug.apk', output / 'TDSpeed-StereoSource-0.1.0-p0-debug.apk')
for name in ('P1_STATUS.md', 'NOTICE.md', 'LICENSE'):
    copy(root / name, output / name)
shutil.copytree(root / 'LICENSES', output / 'LICENSES', dirs_exist_ok=True)
copy(provenance_file, evidence / 'reference-provenance.json')
copy(host / 'reference-inputs.json', evidence / 'reference-inputs.json')
copy(host / 'Testing/Temporary/LastTest.log', evidence / 'host-tests.log')
copy(root / 'dsp-native/host/build-asan/Testing/Temporary/LastTest.log', evidence / 'asan-tests.log')
assert 'Test Passed.' in (evidence / 'asan-tests.log').read_text(encoding='utf-8')

# The APK uses exactly the native bytes exercised by instrumentation, and the AAR
# provides the matching unstripped build. Record every comparison, per ABI.
native_hashes = []
with zipfile.ZipFile(app) as apk, zipfile.ZipFile(native_test) as test, zipfile.ZipFile(aar) as library:
    assert not any('reference-fixtures' in name for name in apk.namelist())
    for abi in ('arm64-v8a', 'x86_64'):
        path = f'lib/{abi}/libtdspeed_probe.so'
        apk_hash = hashlib.sha256(apk.read(path)).hexdigest()
        test_hash = hashlib.sha256(test.read(path)).hexdigest()
        aar_hash = hashlib.sha256(library.read(f'jni/{abi}/libtdspeed_probe.so')).hexdigest()
        image = elf_runtime_image(apk.read(path))
        assert image == elf_runtime_image(test.read(path)), f'App and tested loaded native code differ: {abi}'
        assert image == elf_runtime_image(library.read(f'jni/{abi}/libtdspeed_probe.so')), f'App and AAR loaded native code differ: {abi}'
        native_hashes.append({'abi': abi, 'apkLibrarySha256': apk_hash, 'testApkLibrarySha256': test_hash,
                              'aarLibrarySha256': aar_hash, 'allocatedElfSectionsSha256': image,
                              'loadedNativeImageMatchesAcrossAppTestAndAar': True, 'runtimeTested': abi == 'x86_64'})
        # All three artifacts are built from the same module. AAR may retain debug
        # symbols while the APK strips them; record that boundary explicitly.
package_audit = audit(app)
(output / 'APK-AUDIT.json').write_text(json.dumps(package_audit, indent=2) + '\n', encoding='utf-8')

report_root = root / 'dsp-native/build/outputs/androidTest-results/connected/debug'
reports = list(report_root.glob('*.xml'))
assert len(reports) == 1, 'Expected one current native instrumentation report'
report = ET.parse(reports[0]).getroot()
assert report.attrib['tests'] == '6' and report.attrib['failures'] == '0' and report.attrib['errors'] == '0'
copy(reports[0], evidence / 'android-native-tests.xml')
logs = list(report_root.rglob('logcat-*androidBuildMatchesActualWindowsReferenceFixtures.txt'))
assert len(logs) == 1
matches = re.findall(r'System\.out: (\{[^\n]+\})', logs[0].read_text(encoding='utf-8'))
runtime = json.loads(matches[-1])
assert runtime['result'] == 'passed' and runtime['fixtureSha256'] == provenance['fixtureSha256']
assert 'x86_64' in runtime['nativeIdentity'], 'Record and qualify other runtime ABIs explicitly'
(evidence / 'android-reference-result.json').write_text(json.dumps(runtime, indent=2) + '\n', encoding='utf-8')
copy(logs[0], evidence / 'android-reference-logcat.txt')
unit = root / 'protocol/build/test-results/testDebugUnitTest/TEST-com.fyodor.tdspeed.protocol.SessionLeaseTest.xml'
unit_report = ET.parse(unit).getroot()
assert unit_report.attrib['tests'] == '4' and unit_report.attrib['failures'] == '0' and unit_report.attrib['errors'] == '0'
copy(unit, evidence / 'session-unit-tests.xml')
for module in ('app', 'dsp-native', 'test-source'):
    lint = root / module / 'build/reports/lint-results-debug.txt'
    assert '0 errors,' in lint.read_text(encoding='utf-8')
    copy(lint, evidence / f'{module}-lint.txt')

routing_file = recheck / 'p1-routing-cycles.json'
routing = json.loads(routing_file.read_text(encoding='utf-8-sig'))
assert routing['result'] == 'passed' and routing['apkSha256'] == sha(app)
assert len(routing['cycles']) == 30 and routing['controllerLeaseExpiryPassed']
copy(routing_file, evidence / 'p1-routing-cycles.json')
copy(recheck / 'p1-routing-test.txt', evidence / 'p1-routing-test.txt')
copy(recheck / 'p0-routing-cycles.json', evidence / 'p0-recheck-routing-cycles.json')

validation = {
    'version': version, 'createdAtUtc': datetime.now(timezone.utc).isoformat(),
    'packagedApkSha256': sha(app), 'signing': 'Android debug key; not a production release',
    'approvedArchitectureUnchanged': True, 'pinnedWindowsInputsVerified': 9,
    'desktop': provenance['desktopComparison'], 'addressSanitizer': 'passed',
    'androidDryCore': runtime, 'nativeLibraryIdentity': native_hashes,
    'sessionUnitTests': 4, 'androidNativeTests': 6, 'staticApkAudit': 'passed',
    'routingRegression': {'cycles': 30, 'controllerLeaseExpiry': 'passed', 'runtimeEvidenceMatchesPackagedApk': True},
    'physicalS24Ultra': 'not_tested', 'arm64NumericalRuntime': 'not_tested',
    'commercialMusicApps': 'not_tested', 'androidAuto': 'not_tested', 'pageSize16KiBRuntime': 'not_tested',
    'soak30Minutes': 'not_tested', 'liveVarispeedIntegration': 'P2_pending',
    'milestone': 'P1_dry_core_verified_on_desktop_and_x86_64_emulator; physical_qualification_open',
}
(output / 'VALIDATION.json').write_text(json.dumps(validation, indent=2) + '\n', encoding='utf-8')

excluded = {'.gradle', '.kotlin', '.cxx', '.git', '.idea', 'build', 'artifacts', '__pycache__'}
sources = []
for path in sorted(root.rglob('*')):
    relative = path.relative_to(root)
    if not path.is_file() or path.is_symlink() or any(p in excluded or p.startswith('build-') for p in relative.parts): continue
    if path.name == 'local.properties' or path.suffix in ('.jks', '.keystore'): continue
    sources.append(path)
manifest = {'package': 'TDSpeed Android P1 dry core', 'version': version,
    'createdAtUtc': datetime.now(timezone.utc).isoformat(), 'minSdk': 33, 'targetSdk': 36,
    'abis': ['arm64-v8a', 'x86_64'], 'toolchain': {'gradle': '8.13', 'androidGradlePlugin': '8.13.2',
        'kotlin': '2.2.21', 'ndk': '28.2.13676358', 'cmake': '3.22.1'},
    'files': [{'path': p.relative_to(root).as_posix(), 'sha256': sha(p)} for p in sources]}
(output / 'BUILD-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
source_zip = output / f'TDSpeed-Android-{version}-source.zip'
with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sources: archive.write(path, 'Android/' + path.relative_to(root).as_posix())
    archive.write(output / 'BUILD-MANIFEST.json', 'Android/BUILD-MANIFEST.json')
with zipfile.ZipFile(source_zip) as archive: assert archive.testzip() is None
(output / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(output).as_posix()}\n'
    for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS.txt'), encoding='utf-8')
bundle = output.parent / f'TDSpeed-Android-{version}.zip'
with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(output.rglob('*')):
        if path.is_file(): archive.write(path, 'TDSpeed-Android-P1/' + path.relative_to(output).as_posix())
with zipfile.ZipFile(bundle) as archive: assert archive.testzip() is None
print(json.dumps({'bundle': str(bundle), 'sourceFiles': len(sources), 'validation': validation}, indent=2))
