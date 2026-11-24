"""Original bounded, read-only compiled-KC ABI verifier. No writer/runtime API."""
from pathlib import Path
import hashlib
import json
import struct
import uuid

_PROFILE_SHA256 = 'c4abef6755105ce1b221ee90f2cdc3b5408746b286eb845715ff3ce1f902d990'
_MAX_IMAGE = 256 << 20
_U64 = (1 << 64) - 1


class VerificationError(ValueError):
    """Unknown, malformed or different compiled kernel/profile."""


def _need(condition, reason):
    if not condition:
        raise VerificationError(reason)


def _range(base, size, limit):
    return (type(base) is int and type(size) is int and 0 <= base <= limit
            and 0 <= size <= limit - base)


def _name(raw, variable=False):
    at = raw.find(b'\0')
    if variable:
        _need(at > 0, 'entry string terminator')
    if at >= 0:
        _need(not any(raw[at:]), 'string padding')
        raw = raw[:at]
    _need(0 < len(raw) <= 255 and all(33 <= b <= 126 for b in raw), 'ASCII name')
    return raw.decode('ascii')


def _header(raw, base):
    _need(_range(base, 32, len(raw)), 'header range')
    magic, cpu, sub, kind, count, size, flags, reserved = struct.unpack_from('<8I', raw, base)
    _need(magic == 0xfeedfacf and cpu == 0x100000c and reserved == 0, 'ARM64 header')
    _need(0 < count <= 4096 and size <= 1 << 20 and size >= 8 * count, 'command budget')
    _need(_range(base + 32, size, len(raw)), 'command region')
    at, end = base + 32, base + 32 + size
    segments, entries, uuids = [], [], []
    entry_names, entry_offsets = set(), set()
    section_total = 0
    for _ in range(count):
        _need(_range(at, 8, end), 'command prefix')
        cmd, length = struct.unpack_from('<2I', raw, at)
        _need(length >= 8 and length % 8 == 0 and _range(at, length, end), 'command extent')
        if cmd == 0x19:
            _need(length >= 72, 'segment command')
            name = _name(raw[at + 8:at + 24])
            vm, vs, fo, fs, maxp, prot, n, fl = struct.unpack_from('<4Q4I', raw, at + 24)
            _need(n <= 256 and length == 72 + 80 * n, 'section count')
            section_total += n
            _need(section_total <= 4096, 'total section budget')
            _need(_range(vm, vs, _U64) and fs <= vs and _range(fo, fs, len(raw)), 'segment ranges')
            _need(maxp <= 7 and prot <= 7 and (prot & ~maxp) == 0, 'segment protections')
            sections = []
            for i in range(n):
                off = at + 72 + i * 80
                sn, sg = _name(raw[off:off + 16]), _name(raw[off + 16:off + 32])
                addr, nb, sf, align, reloc, nr, flags_s, r1, r2, r3 = struct.unpack_from('<2Q8I', raw, off + 32)
                _need(_range(addr, nb, _U64) and align <= 63, 'section scalar range')
                # PRELINK placeholder semantic extents are deliberately not consumed.
                sections.append(dict(name=sn, segment=sg, vmaddr=addr, bytes=nb, fileoff=sf,
                                     alignment=align, flags=flags_s, reserved=[r1, r2, r3],
                                     reloff=reloc, nreloc=nr))
            segments.append(dict(name=name, vmaddr=vm, vmsize=vs, fileoff=fo, filesize=fs,
                                 maxprot=maxp, initprot=prot, flags=fl, sections=sections))
        elif cmd == 0x1b:
            _need(length == 24 and not uuids, 'unique UUID')
            uuids.append(str(uuid.UUID(bytes=raw[at + 8:at + 24])))
        elif cmd == 0x80000035:
            _need(length >= 40 and len(entries) < 256, 'fileset budget')
            vm, fo, no, rs = struct.unpack_from('<2Q2I', raw, at + 8)
            _need(rs == 0 and 32 <= no < length and _range(fo, 32, len(raw)) and fo % 8 == 0,
                  'fileset fields')
            _need(not any(raw[at + 32:at + no]), 'fileset string gap')
            name = _name(raw[at + no:at + length], variable=True)
            _need(name not in entry_names and fo not in entry_offsets, 'duplicate fileset identity')
            entry_names.add(name); entry_offsets.add(fo)
            entries.append(dict(name=name, vmaddr=vm, fileoff=fo))
        at += length
    _need(at == end, 'exact load-command fill')
    return dict(cpu=cpu, subtype=sub, filetype=kind, flags=flags, end=end,
                segments=segments, entries=entries, uuids=uuids)


def _one(items, reason):
    _need(len(items) == 1, reason)
    return items[0]


def _mapped(outer, address, count):
    _need(count > 0 and _range(address, count, _U64), 'mapping request')
    segment = _one([s for s in outer['segments'] if address >= s['vmaddr']
                    and count <= s['filesize'] and address - s['vmaddr'] <= s['filesize'] - count],
                   'unique outer VM coverage')
    # Full coverage alone misses strict interior/partial shadows. Consider each
    # other VM extent, including zero-fill tails that have no file bytes.
    for other in outer['segments']:
        if other is segment:
            continue
        _need(not (other['vmsize'] and other['vmaddr'] < address + count
                   and address < other['vmaddr'] + other['vmsize']),
              'additional intersecting outer VM mapping')
    return segment['fileoff'] + address - segment['vmaddr']


def _normalized(segment):
    return {k: ([_normalized(s) for s in v] if k == 'sections' else v)
            for k, v in segment.items() if k != 'fileoff'}


def _verify(raw, profile):
    """Private trusted/test entry; production never accepts a caller profile."""
    _need(type(raw) is bytes and 32 <= len(raw) <= _MAX_IMAGE, 'immutable bounded image')
    outer = _header(raw, 0)
    _need(outer['filetype'] == 12 and outer['subtype'] == profile['subtype'], 'ARM64 FILESET required')
    entry = _one([e for e in outer['entries'] if e['name'] == 'com.apple.kernel'], 'unique kernel member')
    _need(entry['vmaddr'] == profile['kernel_entry_vmaddr'] and entry['fileoff'] >= outer['end'], 'kernel entry layout')
    member = _header(raw, entry['fileoff'])
    _need(member['filetype'] == 2 and member['cpu'] == profile['cpu']
          and member['subtype'] == profile['subtype'] and member['flags'] == profile['member_flags'],
          'embedded kernel header profile')
    _need(member['uuids'] == [profile['uuid']], 'kernel UUID profile')
    _need(bool(member['segments']), 'kernel segments')
    first = member['segments'][0]
    header_bytes = member['end'] - entry['fileoff']
    _need(first['name'] == '__TEXT' and first['vmaddr'] == entry['vmaddr']
          and first['fileoff'] == entry['fileoff'] and first['filesize'] >= header_bytes,
          'kernel header segment')
    _need(_mapped(outer, entry['vmaddr'], header_bytes) == entry['fileoff'], 'outer header mapping')
    executable = _one([s for s in member['segments'] if s['name'] == '__TEXT_EXEC'], 'unique executable segment')
    _need(_normalized(executable) == profile['executable'], 'executable VM layout profile')
    _need(executable['filesize'] > 0, 'nonempty executable segment')
    _need(_mapped(outer, executable['vmaddr'], executable['filesize']) == executable['fileoff'],
          'absolute outer executable mapping')
    names = set()
    for section in executable['sections']:
        _need(section['name'] not in names and section['segment'] == '__TEXT_EXEC', 'unique executable section')
        names.add(section['name'])
        delta = section['vmaddr'] - executable['vmaddr']
        _need(delta >= 0 and section['bytes'] > 0 and delta <= executable['filesize']
              and section['bytes'] <= executable['filesize'] - delta, 'section segment containment')
        _need(section['fileoff'] == executable['fileoff'] + delta, 'absolute section formula')
        _need(_mapped(outer, section['vmaddr'], section['bytes']) == section['fileoff'], 'outer section mapping')
    text = _one([s for s in executable['sections'] if s['name'] == '__text'], 'unique text section')
    # Offsets already refer to the whole KC: never add the member header offset.
    segment_digest = hashlib.sha256(raw[executable['fileoff']:executable['fileoff'] + executable['filesize']]).hexdigest()
    text_digest = hashlib.sha256(raw[text['fileoff']:text['fileoff'] + text['bytes']]).hexdigest()
    _need(segment_digest == profile['segment_sha256'], 'full executable bytes')
    _need(text_digest == profile['text_sha256'], 'full text bytes')
    return dict(status='COMPILED_KERNEL_ABI_MATCH_ONLY', wire_profile=profile['wire_profile'],
                kernel_uuid=profile['uuid'], image_sha256=hashlib.sha256(raw).hexdigest(), image_bytes=len(raw),
                segment_sha256=segment_digest, text_sha256=text_digest,
                kernel_header_fileoff=entry['fileoff'], segment_fileoff=executable['fileoff'],
                text_fileoff=text['fileoff'], runtime_or_boot_admitted=False)


def _pairs(pairs):
    out = {}
    for key, value in pairs:
        _need(key not in out, 'duplicate trusted-profile key')
        out[key] = value
    return out


def _trusted_profile():
    try:
        with Path(__file__).with_name('PROFILE.json').open('rb') as stream:
            raw = stream.read(32769)
        _need(len(raw) <= 32768 and hashlib.sha256(raw).hexdigest() == _PROFILE_SHA256, 'trusted profile hash')
        return json.loads(raw, object_pairs_hook=_pairs)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise VerificationError('trusted profile unavailable') from error


def verify_kernel(exactbytes):
    """Verify bytes against the sole installed/hash-pinned profile; no I/O writer."""
    profile = _trusted_profile()
    result = _verify(exactbytes, profile)
    result['profile_sha256'] = _PROFILE_SHA256
    return result
