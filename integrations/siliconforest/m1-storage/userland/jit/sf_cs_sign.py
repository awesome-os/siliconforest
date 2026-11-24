#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Ad-hoc code signature with chosen flags and entitlements, for test programs (host side).

  sf_cs_sign.py <program> <output> [--runtime] [--entitlement <key> ...]

Replaces the linker's ad-hoc signature of a thin arm64 Mach-O by one that carries the given
CodeDirectory flags (always ad-hoc 0x2; --runtime adds the hardened runtime, 0x10000) and an
XML entitlements blob in which every named key is <true/>: the shape of a vendor program
signed with `codesign -o runtime --entitlements`, without an identity. There is no CMS
signature and no DER entitlements blob: SiliconForest's kernel validates neither a chain nor
a DER blob; its AMFI stand-in reads the XML blob of the process's own signature
(src/Kernel/Extensions/amfi/amfi_entitlements.cpp). macOS would not accept the result where
it wants a notarised program. docs/roadmap/issues/SF-X-claude-code.md.
"""
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'homebrew'))
import hb_macho  # noqa: E402

CS_ADHOC, CS_RUNTIME = 0x2, 0x10000
MAGIC_SUPER, MAGIC_CD, MAGIC_REQS, MAGIC_ENT = 0xfade0cc0, 0xfade0c02, 0xfade0c01, 0xfade7171
SLOT_CD, SLOT_REQS, SLOT_ENT = 0, 2, 5
EXECSEG_MAIN, EXECSEG_ALLOW_UNSIGNED, EXECSEG_JIT = 0x1, 0x10, 0x40


def blob(magic, body):
    return struct.pack('>II', magic, 8 + len(body)) + body


def sign(data, flags, entitlements):
    m = hb_macho.MachO(data)
    if m.fat or not m.codesig:
        raise SystemExit('need a thin Mach-O with a (linker) signature')
    sig = m.signature()
    old = sig['directories'][0]
    off = m.codesig[0]
    if off + m.codesig[1] != len(data) or old['code_limit'] != off:
        raise SystemExit('the signature is not the end of the file')
    ident = sig['identifier'].encode() + b'\0'
    shift = old['page_shift']
    page = 1 << shift
    text = next((struct.unpack_from('<QQQQ', data, o + 24) for c, o, _ in m.commands
                 if c == hb_macho.LC_SEGMENT_64 and data[o + 8:o + 24].rstrip(b'\0') == b'__TEXT'))
    ent = b''
    if entitlements:
        xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" '
               '"http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0">\n<dict>\n'
               + ''.join('\t<key>%s</key>\n\t<true/>\n' % k for k in entitlements) + '</dict>\n</plist>\n')
        ent = blob(MAGIC_ENT, xml.encode())
    reqs = blob(MAGIC_REQS, struct.pack('>I', 0))
    nspecial = SLOT_ENT if ent else SLOT_REQS
    ncode = (off + page - 1) // page
    header = 88
    hash_off = header + len(ident) + nspecial * 32
    cd_len = hash_off + ncode * 32
    parts = [(SLOT_CD, None), (SLOT_REQS, reqs)] + ([(SLOT_ENT, ent)] if ent else [])
    sb_len = 12 + 8 * len(parts) + cd_len + sum(len(b) for _, b in parts[1:])
    size = (sb_len + 15) & ~15
    # the load commands first: they are inside the hashed range
    new = bytearray(data[:off])
    for c, o, _ in m.commands:
        if c == hb_macho.LC_CODE_SIGNATURE:
            struct.pack_into('<II', new, o + 8, off, size)
        elif c == hb_macho.LC_SEGMENT_64 and data[o + 8:o + 24].rstrip(b'\0') == b'__LINKEDIT':
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, o + 24)
            filesize = off + size - fileoff
            struct.pack_into('<QQQQ', new, o + 24, vmaddr, (filesize + 0x3fff) & ~0x3fff, fileoff, filesize)
    execseg = EXECSEG_MAIN
    if 'com.apple.security.cs.allow-jit' in entitlements:
        execseg |= EXECSEG_JIT
    if 'com.apple.security.cs.allow-unsigned-executable-memory' in entitlements:
        execseg |= EXECSEG_ALLOW_UNSIGNED
    special = [b'\0' * 32] * nspecial                     # slot -n at index nspecial - n
    special[nspecial - SLOT_REQS] = hashlib.sha256(reqs).digest()
    if ent:
        special[nspecial - SLOT_ENT] = hashlib.sha256(ent).digest()
    code = b''.join(hashlib.sha256(bytes(new[i * page:min((i + 1) * page, off)])).digest() for i in range(ncode))
    cd = struct.pack('>IIIIIIIIIBBBBIIIIQQQQ', MAGIC_CD, cd_len, 0x20400, flags, hash_off, header, nspecial, ncode, off,
                     32, 2, 0, shift, 0, 0, 0, 0, 0, text[2], text[3], execseg) + ident + b''.join(special) + code
    assert len(cd) == cd_len
    index, body, at = b'', b'', 12 + 8 * len(parts)
    for slot, b in parts:
        b = cd if b is None else b
        index += struct.pack('>II', slot, at)
        body += b
        at += len(b)
    sb = struct.pack('>III', MAGIC_SUPER, sb_len, len(parts)) + index + body
    return bytes(new) + sb + b'\0' * (size - len(sb))


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    flags, ents, rest = CS_ADHOC, [], argv[3:]
    while rest:
        a = rest.pop(0)
        if a == '--runtime':
            flags |= CS_RUNTIME
        elif a == '--entitlement' and rest:
            ents.append(rest.pop(0))
        else:
            raise SystemExit(__doc__)
    out = sign(Path(argv[1]).read_bytes(), flags, ents)
    if hb_macho.verify(out) != 0:
        raise SystemExit('the new signature does not cover the file')
    Path(argv[2]).write_bytes(out)
    s = hb_macho.MachO(out).signature()
    print('%s: flags %#x, identifier %s, entitlements %s' % (argv[2], s['flags'], s['identifier'], ' '.join(ents) or '(none)'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
