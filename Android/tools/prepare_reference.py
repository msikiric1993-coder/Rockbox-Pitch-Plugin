"""Read the existing Release test target; never modify the Windows reference."""
import argparse
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
build, output = args.build.resolve(), args.output.resolve()
source = build.parent / 'project'
android = Path(__file__).resolve().parents[1]
baseline = (android / 'SOURCES.md').read_text(encoding='utf-8')
verified = []
for relative, expected in re.findall(r'\| ((?:Source/|CMakeLists|LICENSE)[^|]*?) \| ([a-f0-9]{64}) \|', baseline):
    actual = hashlib.sha256((source / relative).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f'Reference input changed: {relative}')
    verified.append({'path': relative, 'sha256': actual})
assert len(verified) == 9, f'Expected nine pinned inputs, found {len(verified)}'
ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
root = ET.parse(build / 'TDSpeedTests.vcxproj').getroot()
group = next(g for g in root.findall('m:ItemDefinitionGroup', ns) if "'Release|x64'" in g.attrib.get('Condition', ''))
definitions = group.find('m:ClCompile/m:PreprocessorDefinitions', ns).text
includes = group.find('m:ClCompile/m:AdditionalIncludeDirectories', ns).text
libraries = group.find('m:Link/m:AdditionalDependencies', ns).text
output.mkdir(parents=True, exist_ok=True)
header = ['#pragma once']
for value in definitions.split(';'):
    if not value or value.startswith(('%', 'CMAKE_INTDIR', 'NDEBUG', '_NDEBUG')):
        continue
    name, _, expression = value.partition('=')
    header.append(f'#define {name} {expression or "1"}')
(output / 'ReferenceConfig.h').write_text('\n'.join(header) + '\n', encoding='utf-8')
def quote(value):
    return '[[' + str(value).replace('\\', '/') + ']]'
include_paths = [v for v in includes.split(';') if v and not v.startswith('%')]
library_paths = []
for value in libraries.split(';'):
    if not value or value.startswith('%'):
        continue
    path = build / value
    if '/' in value or '\\' in value:
        assert path.is_file(), f'Build the reference Release libraries first: {path}'
        library_paths.append(quote(path))
    else:
        library_paths.append(quote(value))
(output / 'reference.cmake').write_text('set(REFERENCE_INCLUDES ' + ' '.join(map(quote, include_paths)) + ')\n'
    + 'set(REFERENCE_LIBRARIES ' + ' '.join(library_paths) + ')\n', encoding='utf-8')
verified += [{'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
             for p in (build / 'TDSpeedTests.vcxproj', build / 'TDSpeedVST_artefacts/Release/TDSpeedPreview_SharedCode.lib', build / 'Release/TDSpeedEffectsEngines.lib')]
(output / 'reference-inputs.json').write_text(json.dumps(verified, indent=2) + '\n', encoding='utf-8')
print(f'Pinned source inputs verified: 9; actual Release libraries linked from {build}')
