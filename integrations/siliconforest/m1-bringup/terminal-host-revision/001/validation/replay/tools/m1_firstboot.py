#!/usr/bin/env python3
"""Inspect/package a pinned M1 first-console experiment. Never contact hardware.

An embedded marker and valid Mach-O are byte-level checks, not execution proof.
The bundle remains a review artifact; this tool has no launcher or live backend.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

try:
    from .lab import read_json, regular_file, unique_object, ValidationError
except ImportError:
    from lab import read_json, regular_file, unique_object, ValidationError

PUREDARWIN_PIN = 'e7aa00a60f0b56ef35aa24252e81467787bf71ea'
M1N1_PIN = '121fcbf35d4501814b58ca5347926a4173b6a512'
MARKER = 'DARWIN_LAB_M1_EARLY_CONSOLE'
CONSOLE_BYTES = (b'HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm '
                 b'next=park kernel_init=not_started\r\n\0')
VERSION_TOKEN = 'DEBUG_ARM64_T8103'
PACKAGE_ATTRIBUTE = 'packages.x86_64-linux.kernel-arm64-t8103-firstboot'
REVIEWED_PATCH_MANIFEST = Path(__file__).resolve().parents[1] / 'integrations/puredarwin/m1-firstboot/manifest.json'
UNSUPPORTED_CACHE_SYMBOLS = (b'_CleanPoC_DcacheRegion_Force_nopreempt',
    b'_CleanPoC_DcacheRegion_Force_nopreempt_nohid', b'_enable_dc_mva_ops',
    b'_disable_dc_mva_ops', b'_darwin_lab_m1_unsupported_cache')
MAX_ARTIFACT_BYTES = 512 * 1024 * 1024
SHA256 = re.compile(r'[0-9a-f]{64}\Z')
ARTIFACT_NAMES = {
    'kernel': 'kernel.macho', 'm1n1': 'm1n1.bin',
    'source_patch': 'source.patch', 'build_record': 'build-record.json',
    'm1n1_build_record': 'm1n1-build-record.json',
}


def artifact_bytes(path: Path) -> bytes:
    regular_file(path)
    if path.stat().st_size > MAX_ARTIFACT_BYTES:
        raise ValidationError('Artifact exceeds inspection size limit')
    data = path.read_bytes()
    if not data:
        raise ValidationError('Artifact is empty')
    return data


def inspect_kernel(data: bytes) -> dict:
    if len(data) < 32:
        raise ValidationError('Truncated Mach-O header')
    magic, cpu, subtype, kind, count, command_bytes, flags, reserved = struct.unpack_from('<8I', data)
    if magic != 0xfeedfacf or cpu != 0x100000c or subtype != 0 or kind != 2:
        raise ValidationError('Expected plain ARM64 MH_EXECUTE first-console kernel')
    end = 32 + command_bytes
    if not 0 < count <= 4096 or command_bytes > 1024 * 1024 or end > len(data):
        raise ValidationError('Invalid Mach-O load-command bounds')
    offset = 32
    segments = []
    entries = []
    symbol_commands = []
    for _ in range(count):
        if offset + 8 > end:
            raise ValidationError('Truncated load command')
        cmd, size = struct.unpack_from('<II', data, offset)
        if size < 8 or size % 8 or offset + size > end:
            raise ValidationError('Invalid load-command size')
        if cmd == 0x19:
            if size < 72:
                raise ValidationError('Truncated segment command')
            name = data[offset+8:offset+24].split(b'\0')[0].decode('ascii', errors='strict')
            vm, vm_size, fileoff, file_size, maxprot, prot, sections, segflags = struct.unpack_from('<4Q4I', data, offset+24)
            if size != 72 + 80 * sections or fileoff + file_size > len(data) or file_size > vm_size or vm + vm_size > 1 << 64:
                raise ValidationError('Invalid segment bounds')
            segments.append({'name': name, 'vmaddr': vm, 'vmsize': vm_size,
                             'fileoff': fileoff, 'filesize': file_size, 'initprot': prot})
        elif cmd == 5:
            if size != 288 or struct.unpack_from('<II', data, offset+8) != (6, 68):
                raise ValidationError('Unsupported ARM64 thread entry command')
            entries.append(struct.unpack_from('<Q', data, offset+272)[0])
        elif cmd == 2:
            if size != 24:
                raise ValidationError('Invalid symbol-table command')
            symbol_commands.append(struct.unpack_from('<4I', data, offset+8))
        offset += size
    if offset != end or len(entries) != 1:
        raise ValidationError('Expected exactly one bounded LC_UNIXTHREAD entry')
    if not segments or max(s['vmaddr']+s['vmsize'] for s in segments) - min(s['vmaddr'] for s in segments) > MAX_ARTIFACT_BYTES:
        raise ValidationError('Loader virtual image exceeds inspection size limit')
    entry = entries[0]
    if not any(s['initprot'] & 4 and s['vmaddr'] <= entry < s['vmaddr'] + s['filesize'] for s in segments):
        raise ValidationError('Entry is outside file-backed executable segments')
    if MARKER.encode() not in data or VERSION_TOKEN.encode() not in data:
        raise ValidationError('Missing first-console marker or T8103 DEBUG version identity')
    if len(symbol_commands) != 1:
        raise ValidationError('Expected one inspectable DEBUG symbol table')
    symoff, count, stroff, strsize = symbol_commands[0]
    if symoff + count * 16 > len(data) or stroff + strsize > len(data):
        raise ValidationError('Symbol table exceeds file bounds')
    strings = data[stroff:stroff+strsize]
    symbols = {}
    for i in range(count):
        index, kind, section, desc, address = struct.unpack_from('<IBBHQ', data, symoff+i*16)
        if index >= strsize:
            raise ValidationError('Symbol name exceeds string table')
        end_name = strings.find(b'\0', index)
        if end_name < 0:
            raise ValidationError('Unterminated symbol name')
        name = strings[index:end_name]
        if name in (b'__start', b'_darwin_lab_m1_first_entry', *UNSUPPORTED_CACHE_SYMBOLS) and kind & 0x0e == 0x0e:
            if name in symbols:
                raise ValidationError('Duplicate diagnostic entry symbol')
            symbols[name] = address
    if symbols.get(b'__start') != entry or b'_darwin_lab_m1_first_entry' not in symbols:
        raise ValidationError('Entry symbols do not match the diagnostic contract')

    def instruction(address):
        for segment in segments:
            delta = address - segment['vmaddr']
            if segment['initprot'] & 4 and 0 <= delta <= segment['filesize'] - 4:
                return struct.unpack_from('<I', data, segment['fileoff']+delta)[0]
        raise ValidationError('Instruction is outside executable file bytes')

    branch = instruction(entry)
    if branch & 0xfc000000 != 0x14000000:
        raise ValidationError('XNU entry does not directly branch to the diagnostic')
    displacement = branch & 0x03ffffff
    if displacement & 0x02000000:
        displacement -= 1 << 26
    diagnostic = entry + displacement * 4
    if diagnostic != symbols[b'_darwin_lab_m1_first_entry']:
        raise ValidationError('XNU entry branches to a different routine')
    # DAIFSet #0xf; mov x19,#0; mrs x9,CurrentEL. This checks the linked target,
    # not a marker copied into an unrelated data section.
    prefix = tuple(instruction(diagnostic+i*4) for i in range(3))
    if prefix != (0xd5034fdf, 0xd2800013, 0xd5384249):
        raise ValidationError('Diagnostic instruction prefix does not match reviewed source')
    markers = []
    for segment in segments:
        if not segment['initprot'] & 4:
            continue
        body = data[segment['fileoff']:segment['fileoff']+segment['filesize']]
        cursor = body.find(CONSOLE_BYTES)
        while cursor >= 0:
            markers.append(segment['vmaddr'] + cursor)
            cursor = body.find(CONSOLE_BYTES, cursor + 1)
    if len(markers) != 1 or not diagnostic < markers[0] < diagnostic + 2048:
        raise ValidationError('Expected one nearby executable diagnostic console literal')
    park = markers[0] - 4
    if instruction(park) != 0x14000000 or instruction(park-4) != 0xd2801c93:
        raise ValidationError('Diagnostic final park must follow the UART timeout state and self-loop')
    if any(name not in symbols for name in UNSUPPORTED_CACHE_SYMBOLS):
        raise ValidationError('Missing explicit unsupported cache operation bindings')
    failstop = symbols[UNSUPPORTED_CACHE_SYMBOLS[-1]]
    if any(symbols[name] != failstop for name in UNSUPPORTED_CACHE_SYMBOLS):
        raise ValidationError('Cache helpers do not bind to the unsupported failure path')
    if tuple(instruction(failstop+i*4) for i in range(3)) != (0xd5034fdf, 0xd2801df3, 0x14000000):
        raise ValidationError('Unsupported cache operation must mask DAIF and never return')
    return {'filetype': 'MH_EXECUTE', 'architecture': 'ARM64', 'entry': entry,
            'segments': segments, 'embedded_marker': MARKER,
            'diagnostic_entry': diagnostic, 'park_address': park, 'entry_branch_verified': True,
            'unsupported_cache_failstop': failstop, 'unsupported_cache_bindings_verified': True,
            'normal_kernel_initialization_supported': False,
            'marker_execution_observed': False}


def inspect_m1n1(data: bytes) -> dict:
    if len(data) < 0x828 or data[0x808:0x810] != b'AURALOAD':
        raise ValidationError('Missing pinned m1n1 raw-image load header')
    version, flags, footprint, run_base = struct.unpack_from('<IIQQ', data, 0x810)
    if version != 1 or flags != 1 or not footprint or footprint % 0x4000 or run_base:
        raise ValidationError('Unsupported or pre-relocated m1n1 image')
    tag = ('##m1n1_ver##'+M1N1_PIN+'\0').encode()
    if data.count(tag) != 1:
        raise ValidationError('Expected the complete pinned m1n1 BUILD_TAG exactly once')
    return {'header_version': version, 'footprint': footprint,
            'run_base': run_base, 'build_tag': M1N1_PIN,
            'physical_execution_observed': False}


def inspect_manifest(path: Path) -> tuple[dict, dict[str, bytes]]:
    manifest = read_json(path)
    if manifest.get('schema_version') != 1 or manifest.get('target') != 'm1-mbp13-j293':
        raise ValidationError('Unsupported target manifest')
    if manifest.get('source_pins') != {'puredarwin': PUREDARWIN_PIN, 'm1n1': M1N1_PIN}:
        raise ValidationError('Unexpected source pins')
    if manifest.get('machine_config') != 'T8103' or manifest.get('kernel_config') != 'DEBUG':
        raise ValidationError('Expected T8103 DEBUG build selection')
    artifacts = manifest.get('artifacts')
    if not isinstance(artifacts, dict) or set(artifacts) != set(ARTIFACT_NAMES):
        raise ValidationError('Missing required artifact or build receipt')
    contents = {}
    identities = {}
    for key, record in artifacts.items():
        if not isinstance(record, dict) or not isinstance(record.get('path'), str) or not record['path']:
            raise ValidationError('Missing artifact path')
        expected = record.get('sha256')
        if not isinstance(expected, str) or not SHA256.fullmatch(expected):
            raise ValidationError('Missing immutable artifact SHA-256')
        artifact = Path(record['path'])
        if not artifact.is_absolute():
            artifact = path.parent / artifact
        data = artifact_bytes(artifact)
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValidationError('Artifact hash mismatch: ' + key)
        contents[key] = data
        identities[key] = {'sha256': expected, 'bytes': len(data)}
    kernel = inspect_kernel(contents['kernel'])
    m1n1 = inspect_m1n1(contents['m1n1'])
    reviewed = read_json(REVIEWED_PATCH_MANIFEST)
    if reviewed.get('pin') != PUREDARWIN_PIN or reviewed.get('patch_sha256') != identities['source_patch']['sha256']:
        raise ValidationError('Packet source patch differs from the reviewed candidate')
    build = json.loads(contents['build_record'], object_pairs_hook=unique_object)
    expected = {'source_pin': PUREDARWIN_PIN,
                'patch_sha256': identities['source_patch']['sha256'],
                'kernel_sha256': identities['kernel']['sha256'],
                'package_attribute': PACKAGE_ATTRIBUTE,
                'machine_config': 'T8103', 'kernel_config': 'DEBUG', 'returncode': 0}
    if (not isinstance(build, dict) or type(build.get('returncode')) is not int or
        any(build.get(k) != v for k, v in expected.items())):
        raise ValidationError('Kernel build receipt does not match the packet')
    m1n1_build = json.loads(contents['m1n1_build_record'], object_pairs_hook=unique_object)
    files = m1n1_build.get('files') if isinstance(m1n1_build, dict) else None
    image = files.get('build/m1n1.bin') if isinstance(files, dict) else None
    if (not isinstance(m1n1_build, dict) or m1n1_build.get('commit') != M1N1_PIN or
        m1n1_build.get('build_tag') != M1N1_PIN or
        type(m1n1_build.get('returncode')) is not int or m1n1_build.get('returncode') != 0 or
        m1n1_build.get('source_clean') is not True or not isinstance(image, dict) or
        image.get('sha256') != identities['m1n1']['sha256']):
        raise ValidationError('m1n1 build receipt does not match the packet')
    report = {'status': 'ARTIFACT_BYTES_VERIFIED_FOR_REVIEW', 'target': manifest['target'],
              'artifacts': identities, 'kernel': kernel, 'm1n1': m1n1,
              'reviewed_patch_sha256': reviewed['patch_sha256'],
              'reviewed_manifest_sha256': hashlib.sha256(REVIEWED_PATCH_MANIFEST.read_bytes()).hexdigest(),
              'hardware_boot_observed': False, 'execution_permitted': False,
              'source_and_build_claims_independently_certified': False,
              'limits': ['Embedded identity is not runtime proof.',
                         'Build record and source patch are preserved for independent review.',
                         'Physical identity, firmware, transport, matching proxy ABI and recovery still require verification.']}
    return report, contents


def prepare_bundle(manifest: Path, destination: Path) -> dict:
    report, contents = inspect_manifest(manifest)
    # Exclusive creation prevents accidental replacement of an earlier packet.
    destination.mkdir(parents=False, exist_ok=False)
    packaged = read_json(manifest)
    for key, name in ARTIFACT_NAMES.items():
        (destination / name).write_bytes(contents[key])
        packaged['artifacts'][key]['path'] = name
    (destination / 'manifest.json').write_text(json.dumps(packaged, indent=2)+'\n')
    (destination / 'inspection.json').write_text(json.dumps(report, indent=2)+'\n')
    # Recheck the actual copies, rather than reporting the input identities alone.
    inspect_manifest(destination / 'manifest.json')
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--bundle-directory', type=Path)
    args = parser.parse_args(argv)
    try:
        result = prepare_bundle(args.manifest, args.bundle_directory) if args.bundle_directory else inspect_manifest(args.manifest)[0]
        print(json.dumps(result, indent=2))
        return 0
    except (ValidationError, OSError, UnicodeError, struct.error, json.JSONDecodeError) as exc:
        print(json.dumps({'status': 'REJECTED', 'error': str(exc), 'execution_permitted': False}))
        return 1


if __name__ == '__main__':
    sys.exit(main())
