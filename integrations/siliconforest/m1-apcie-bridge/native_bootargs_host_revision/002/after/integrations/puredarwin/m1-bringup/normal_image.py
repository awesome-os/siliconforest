"""Inspect a continuing ARM64 kernel collection; no device access or fixups.

The pinned XNU arm_init applies collection fixups itself. A bare Mach-O instead
expects an iBoot sliding contract that the stock m1n1 loader does not implement.
Do not silently accept the old entry-and-park image as a continuing kernel.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct

MAX_IMAGE = 512 << 20
MAX_MANIFEST = 4 << 20
PINS = {
    'puredarwin': 'e7aa00a60f0b56ef35aa24252e81467787bf71ea',
    'corecrypto': 'ea8e6afec1a8666a077c110fc4148fb1ac293ae8',
    'm1n1': '121fcbf35d4501814b58ca5347926a4173b6a512',
}
KC_LINK = 0xfffffe0006000000


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, 'Duplicate JSON key')
        out[key] = value
    return out


def bounded_file(path, maximum=MAX_IMAGE):
    path = Path(path)
    require(not path.is_symlink() and path.is_file(), 'Expected regular artifact')
    require(0 < path.stat().st_size <= maximum, 'Artifact size is unbounded or empty')
    return path.read_bytes()


def commands(data, base=0):
    require(base >= 0 and base + 32 <= len(data), 'Truncated Mach-O header')
    magic, cpu, subtype, kind, count, nbytes, flags, reserved = struct.unpack_from('<8I', data, base)
    require((magic, cpu, subtype, reserved) == (0xfeedfacf, 0x100000c, 0, 0),
            'Expected little-endian ARM64 Mach-O')
    require(0 < count <= 4096 and count * 8 <= nbytes <= 1 << 20,
            'Unbounded Mach-O load commands')
    end = base + 32 + nbytes
    require(end <= len(data), 'Truncated Mach-O load commands')
    result, cursor = [], base + 32
    for _ in range(count):
        require(cursor + 8 <= end, 'Truncated load command')
        cmd, size = struct.unpack_from('<2I', data, cursor)
        require(size >= 8 and size % 8 == 0 and cursor + size <= end,
                'Invalid load-command size')
        result.append((cmd, cursor, size))
        cursor += size
    require(cursor == end, 'Load commands do not fill header')
    return kind, flags, result


def inspect(data):
    require(32 <= len(data) <= MAX_IMAGE, 'Invalid kernel collection size')
    kind, flags, cmds = commands(data)
    require(kind == 12, 'Continuing loader requires MH_FILESET, not a bare/diagnostic kernel')
    require(b'DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm' not in data,
            'Old diagnostic park is present')
    require(b'DEBUG_ARM64_T8103' in data, 'Missing native T8103 DEBUG identity')
    segments, members, entries, fixups = [], {}, [], []
    for cmd, off, size in cmds:
        if cmd == 0x19:
            require(size >= 72, 'Truncated segment')
            name = data[off+8:off+24].split(b'\0', 1)[0].decode('ascii')
            vm, nvm, foff, nfile, maxprot, prot, sections, _ = struct.unpack_from('<4Q4I', data, off+24)
            require(size == 72 + sections * 80 and foff + nfile <= len(data) and
                    nfile <= nvm and vm + nvm < 1 << 64 and prot & ~maxprot == 0,
                    'Invalid segment bounds/protection')
            require(name != 'PYLD', 'Payload truncation is unsupported')
            segments.append(dict(name=name, vmaddr=vm, vmsize=nvm,
                                 fileoff=foff, filesize=nfile, initprot=prot))
        elif cmd == 5:
            require(size == 288 and struct.unpack_from('<2I', data, off+8) == (6, 68),
                    'Unsupported ARM64 thread entry')
            entries.append(struct.unpack_from('<Q', data, off+272)[0])
        elif cmd == 0x80000035:
            require(size >= 40, 'Truncated fileset entry')
            vm, foff, nameoff, reserved = struct.unpack_from('<QQII', data, off+8)
            require(reserved == 0 and 32 <= nameoff < size, 'Invalid fileset entry name')
            raw = data[off+nameoff:off+size]
            require(b'\0' in raw, 'Unterminated fileset entry name')
            name = raw.split(b'\0', 1)[0].decode('ascii')
            require(name and name not in members, 'Duplicate/empty fileset member')
            memberkind, memberflags, membercmds = commands(data, foff)
            require(memberkind in (2, 8, 11) and memberflags & 0x80000000,
                    'Member is not marked as part of a kernel collection')
            if name == 'com.apple.kernel':
                # arm_init reads the very first load command as __TEXT to
                # compute its slide; the fileset name alone is insufficient.
                firstcmd, firstoff, firstsize = membercmds[0]
                require(firstcmd == 0x19 and firstsize >= 72 and
                        data[firstoff+8:firstoff+24].rstrip(b'\0') == b'__TEXT' and
                        struct.unpack_from('<Q', data, firstoff+24)[0] == vm and
                        struct.unpack_from('<Q', data, firstoff+40)[0] == foff,
                        'Kernel first segment does not satisfy XNU slide contract')
                text_end = vm + struct.unpack_from('<Q', data, firstoff+32)[0]
                for membercmd, memberoff, membersize in membercmds:
                    if (membercmd == 0x19 and membersize >= 72 and
                            data[memberoff+8:memberoff+24].rstrip(b'\0') == b'__DATA_CONST'):
                        require(text_end <= struct.unpack_from('<Q', data, memberoff+24)[0],
                                'Embedded kernel TEXT overlaps writable startup DATA_CONST')
            members[name] = dict(vmaddr=vm, fileoff=foff, filetype=memberkind)
        elif cmd == 0x80000034:
            require(size == 16, 'Invalid chained fixups command')
            foff, nfile = struct.unpack_from('<2I', data, off+8)
            require(nfile >= 28 and foff + nfile <= len(data), 'Truncated collection fixups')
            fixups.append((foff, nfile))
    require(segments and len(entries) == 1 and len(fixups) == 1,
            'Expected segments, one entry, and one chained-fixup payload')
    low = min(s['vmaddr'] for s in segments)
    high = max(s['vmaddr'] + s['vmsize'] for s in segments)
    require(low == KC_LINK and 0 < high - low <= MAX_IMAGE, 'Unsupported collection link/span')
    require(any(s['fileoff'] == 0 and s['vmaddr'] == KC_LINK and s['filesize'] >= 32
                for s in segments), 'Collection header is not mapped at VM_KERNEL_LINK_ADDRESS')
    require('com.apple.kernel' in members and members['com.apple.kernel']['filetype'] == 2,
            'Missing executable kernel member')
    for name, member in members.items():
        require(any(s['vmaddr'] <= member['vmaddr'] < s['vmaddr'] + s['filesize'] and
                    s['fileoff'] + member['vmaddr'] - s['vmaddr'] == member['fileoff']
                    for s in segments), 'Fileset header does not match loaded bytes: ' + name)
    entry = entries[0]
    executable = [s for s in segments if s['initprot'] & 4 and
                  s['vmaddr'] <= entry <= s['vmaddr'] + s['filesize'] - 4]
    require(len(executable) == 1, 'Entry is not uniquely file-backed executable code')
    s = executable[0]
    opcode, = struct.unpack_from('<I', data, s['fileoff'] + entry - s['vmaddr'])
    require(opcode not in (0, 0x14000000), 'Entry is empty or a self-loop')
    # load_raw/prepare_image apply segments in command order. Reject later zero
    # fills that destroy a previously loaded member/header/instruction region.
    for i, later in enumerate(segments):
        for prior in segments[:i]:
            lo = max(later['vmaddr'], prior['vmaddr'])
            hi = min(later['vmaddr'] + later['filesize'], prior['vmaddr'] + prior['filesize'])
            require(lo >= hi or later['fileoff'] + lo - later['vmaddr'] ==
                    prior['fileoff'] + lo - prior['vmaddr'],
                    'Overlapping file-backed segments map different bytes')
        lo, hi = later['vmaddr'] + later['filesize'], later['vmaddr'] + later['vmsize']
        if hi <= lo:
            continue
        for prior in segments[:i]:
            require(not (max(lo, prior['vmaddr']) < min(hi, prior['vmaddr'] + prior['filesize'])),
                    'Later segment zero-fill destroys earlier file bytes')
    diagnostics = {}
    _, _, kernelcmds = commands(data, members['com.apple.kernel']['fileoff'])
    wanted = {b'_darwin_lab_m1_progress': 8, b'_debug_buf_base': 8,
              b'_debug_buf_ptr': 8, b'_debug_buf_size': 4, b'_PE_kputc': 8}
    for cmd, off, size in kernelcmds:
        if cmd != 2:
            continue
        require(size == 24, 'Invalid kernel symbol table command')
        symoff, count, stroff, strsize = struct.unpack_from('<4I', data, off+8)
        require(symoff + count*16 <= len(data) and stroff + strsize <= len(data),
                'Kernel symbol table exceeds collection bounds')
        strings = data[stroff:stroff+strsize]
        for index in range(count):
            nameoff, kind, _, _, address = struct.unpack_from('<IBBHQ', data, symoff+index*16)
            require(nameoff < strsize, 'Kernel symbol name exceeds table')
            endname = strings.find(b'\0', nameoff)
            require(endname >= 0, 'Unterminated kernel symbol')
            name = bytes(strings[nameoff:endname])
            if name in wanted and kind & 0x0e == 0x0e:
                require(name.decode() not in diagnostics and
                        any(s['vmaddr'] <= address and address+wanted[name] <= s['vmaddr']+s['vmsize']
                            and not s['initprot'] & 4 for s in segments), 'Invalid diagnostic data symbol')
                diagnostics[name.decode()] = dict(address=address, bytes=wanted[name])
    return dict(filetype='MH_FILESET', architecture='ARM64', entry=entry,
                segments=segments, members=members, vmin=low, vmax=high,
                diagnostic_symbols=diagnostics,
                kernel_header_vmaddr=members['com.apple.kernel']['vmaddr'],
                fixups='XNU arm_slide_rebase_and_sign_image; loader must not pre-apply',
                hardware_boot_observed=False, command_line_observed=False)


def verify_manifest(path):
    path = Path(path).resolve(strict=True)
    doc = json.loads(bounded_file(path, MAX_MANIFEST), object_pairs_hook=unique)
    require(doc.get('schema_version') == 1 and doc.get('target') == 'm1-mbp13-j293' and
            doc.get('purpose') == 'continuing-xnu' and doc.get('source_pins') == PINS,
            'Unexpected continuing-kernel manifest or pins')
    artifacts = doc.get('artifacts')
    require(isinstance(artifacts, dict) and set(artifacts) in
            ({'kernel', 'source_patch', 'build_record'},
             {'kernel', 'source_patch', 'build_record', 'ramdisk'}), 'Unexpected artifact set')
    contents, hashes = {}, {}
    for name, item in artifacts.items():
        require(isinstance(item, dict) and set(item) == {'path', 'sha256', 'bytes'},
                'Invalid artifact receipt')
        rel = Path(item['path'])
        require(not rel.is_absolute() and '..' not in rel.parts and str(rel) == item['path'],
                'Invalid artifact path')
        source = path.parent / rel
        require(source.resolve(strict=True).is_relative_to(path.parent), 'Artifact escapes packet')
        require(not any(p.is_symlink() for p in (source, *source.parents)
                        if p != path.parent and p.is_relative_to(path.parent)), 'Artifact path uses symlink')
        raw = bounded_file(source)
        require(type(item['bytes']) is int and len(raw) == item['bytes'] and
                isinstance(item['sha256'], str) and re.fullmatch('[0-9a-f]{64}', item['sha256']) and
                sha(raw) == item['sha256'], 'Artifact hash/size mismatch: ' + name)
        contents[name], hashes[name] = raw, sha(raw)
    record = json.loads(contents['build_record'], object_pairs_hook=unique)
    require(record.get('source_commit') == PINS['puredarwin'] and
            record.get('corecrypto_commit') == PINS['corecrypto'] and
            record.get('source_patch_sha256') == hashes['source_patch'], 'Build record provenance mismatch')
    require(record.get('status') == 'BUILT_NOT_BOOTED' and
            type(record.get('build_exit_code')) is int and record['build_exit_code'] == 0 and
            record.get('kernel_sha256') == hashes['kernel'], 'Build result or kernel digest mismatch')
    for key, suffix in (('drv_path', r'\.drv'), ('output_path', '')):
        require(isinstance(record.get(key), str) and re.fullmatch(
            r'/nix/store/[0-9a-z]{32}-[A-Za-z0-9+._?=-]+' + suffix, record[key]),
            'Missing concrete Nix build output: ' + key)
    return dict(kernel=inspect(contents['kernel']), artifacts=hashes,
                source_pins=PINS, purpose=doc['purpose'], hardware_boot_observed=False), contents
