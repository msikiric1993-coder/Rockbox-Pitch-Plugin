"""Audit the actual APK for leaked framework stubs and native ELF page alignment."""
import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path


def uleb(data, pos):
    value = shift = 0
    while True:
        b = data[pos]
        pos += 1
        value |= (b & 127) << shift
        if not b & 128:
            return value, pos
        shift += 7


def defined_classes(dex):
    string_count, string_off = struct.unpack_from('<II', dex, 56)
    strings = []
    for i in range(string_count):
        offset = struct.unpack_from('<I', dex, string_off + i * 4)[0]
        _, offset = uleb(dex, offset)
        strings.append(dex[offset:dex.index(b'\0', offset)].decode('utf-8', 'replace'))
    _, type_off = struct.unpack_from('<II', dex, 64)
    class_count, class_off = struct.unpack_from('<II', dex, 96)
    for i in range(class_count):
        type_index = struct.unpack_from('<I', dex, class_off + i * 32)[0]
        descriptor_index = struct.unpack_from('<I', dex, type_off + type_index * 4)[0]
        yield strings[descriptor_index]


def elf_load_alignments(elf):
    assert elf[:4] == b'\x7fELF' and elf[4] == 2 and elf[5] == 1, 'Expected little-endian ELF64'
    header_offset = struct.unpack_from('<Q', elf, 32)[0]
    entry_size, entry_count = struct.unpack_from('<HH', elf, 54)
    return [struct.unpack_from('<Q', elf, header_offset + i * entry_size + 48)[0]
            for i in range(entry_count)
            if struct.unpack_from('<I', elf, header_offset + i * entry_size)[0] == 1]


def elf_runtime_image(elf):
    """Hash every allocated section, including zero-fill layout, across debug stripping."""
    assert elf[:6] == b'\x7fELF\x02\x01', 'Expected little-endian ELF64'
    offset = struct.unpack_from('<Q', elf, 40)[0]
    entry_size, count, names_index = struct.unpack_from('<HHH', elf, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', elf, offset + i * entry_size) for i in range(count)]
    names = sections[names_index]
    names_data = elf[names[4]:names[4] + names[5]]
    allocated = []
    for name, kind, flags, address, file_offset, size, _, _, alignment, _ in sections:
        if not flags & 2: continue  # SHF_ALLOC: actual loaded code/data, not debug symbols.
        title = names_data[name:names_data.index(b'\0', name)].decode('ascii')
        allocated.append({'name': title, 'type': kind, 'flags': flags, 'address': address,
                          'size': size, 'alignment': alignment,
                          'sha256': None if kind == 8 else hashlib.sha256(elf[file_offset:file_offset+size]).hexdigest()})
    assert allocated
    return hashlib.sha256(json.dumps(allocated, sort_keys=True).encode('utf-8')).hexdigest()


def audit(path):
    libraries = []
    with zipfile.ZipFile(path) as apk:
        assert apk.testzip() is None
        for name in apk.namelist():
            if name.endswith('.dex'):
                leaked = [c for c in defined_classes(apk.read(name)) if c.startswith(('Landroid/media/AudioFocusInfo;', 'Landroid/media/audiopolicy/AudioPolicy'))]
                assert not leaked, f'Framework stubs leaked into APK: {leaked}'
            if name.startswith('lib/') and name.endswith('.so'):
                assert name.split('/')[1] in ('arm64-v8a', 'x86_64'), f'Unsupported ABI: {name}'
                alignments = elf_load_alignments(apk.read(name))
                assert alignments and min(alignments) >= 16_384, f'ELF alignment is too small: {name} {alignments}'
                libraries.append({'path': name, 'loadSegmentAlignmentBytes': alignments})
        assert any(x['path'] == 'lib/arm64-v8a/libtdspeed_probe.so' for x in libraries)
        assert any(x['path'] == 'lib/x86_64/libtdspeed_probe.so' for x in libraries)
    return {'apk': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'frameworkStubsAbsent': True, 'nativeLibraries': libraries,
            'qualification': 'Static package audit only; not a 16 KiB runtime test'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('apk', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = json.dumps(audit(args.apk), indent=2) + '\n'
    if args.output:
        args.output.write_text(report, encoding='utf-8')
    print(report)
