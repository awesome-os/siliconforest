#!/usr/bin/env python3
# Recompute the page hashes of the ad-hoc (linker-made) code signature of thin 64-bit Mach-O
# files, in place.  install_name_tool rewrites load commands in the first page after the linker
# has signed the file, which leaves that page's hash stale; XNU then reports "Text page
# corruption detected" for any crash whose pc or caller is near the start of such an image.
# There is no codesign in the cross toolchain; an ad-hoc signature is only a table of hashes,
# so rewriting the table is all that re-signing means here.
#   cs-rehash.py [--check] file...     --check: report only, exit 1 if any page hash is stale
import hashlib, struct, sys

def process(path, check):
    d = bytearray(open(path, 'rb').read())
    magic, _, _, _, ncmds = struct.unpack_from('<IiiII', d, 0)
    if magic != 0xfeedfacf:
        raise SystemExit(f'{path}: not a thin 64-bit Mach-O')
    off, sig = 32, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', d, off)
        if cmd == 0x1d:                      # LC_CODE_SIGNATURE
            sig = struct.unpack_from('<I', d, off + 8)[0]
        off += size
    if sig is None:
        raise SystemExit(f'{path}: no LC_CODE_SIGNATURE')
    if struct.unpack_from('>I', d, sig)[0] != 0xfade0cc0:
        raise SystemExit(f'{path}: no embedded signature superblob')
    count = struct.unpack_from('>I', d, sig + 8)[0]
    stale = pages = 0
    for i in range(count):
        blob = sig + struct.unpack_from('>I', d, sig + 12 + 8 * i + 4)[0]
        if struct.unpack_from('>I', d, blob)[0] != 0xfade0c02:     # CodeDirectory
            raise SystemExit(f'{path}: signature has a blob that is not a CodeDirectory; not ad-hoc')
        hash_off, _, _, n_code, code_limit, hash_size, hash_type, _, page_shift = struct.unpack_from('>IIIIIBBBB', d, blob + 16)
        make = {1: hashlib.sha1, 2: hashlib.sha256}[hash_type]
        page = 1 << page_shift
        for p in range(n_code):
            slot = blob + hash_off + p * hash_size
            want = make(bytes(d[p * page:min((p + 1) * page, code_limit)])).digest()[:hash_size]
            pages += 1
            if bytes(d[slot:slot + hash_size]) != want:
                stale += 1
                d[slot:slot + hash_size] = want
    if stale and not check:
        open(path, 'wb').write(d)
    print(f'{path}: {pages} pages, {stale} stale hash{"es" if stale != 1 else ""}{"" if check or not stale else " rewritten"}')
    return stale

args = sys.argv[1:]
check = args[:1] == ['--check']
total = sum(process(p, check) for p in args[1 if check else 0:])
sys.exit(1 if check and total else 0)
