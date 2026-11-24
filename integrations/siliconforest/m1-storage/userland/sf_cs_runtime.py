#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A copy of a Mach-O program without the "hardened runtime" bit in its code signature.

  sf_cs_runtime.py <program> <output>        prints the flags of every CodeDirectory before and after

A diagnostic, and it did not help, because the hardened runtime was never the cause: the
Claude Code CLI was ended on SiliconForest by libxpc (xpc_copy_description of a connection,
reached from Security.framework's SecItemCopyMatching), not by the kernel, and a copy made by
this tool was ended the same way (boot dr45). docs/roadmap/issues/SF-X-claude-code.md.
The change: bit 0x10000 cleared in the flags word of each CodeDirectory (big-endian u32 at
offset 12 of the blob). Nothing else: not the code, not the page hashes. The vendor's CMS
signature no longer matches (it covered that word); this kernel does not check it, macOS would.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'homebrew'))
import hb_macho  # noqa: E402

CS_RUNTIME = 0x10000


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    data = bytearray(Path(argv[1]).read_bytes())
    sig = hb_macho.MachO(bytes(data)).signature()
    if not sig or 'error' in sig or not sig['directories']:
        raise SystemExit('no embedded code signature')
    for d in sig['directories']:
        at = d['at'] + 12
        flags, = struct.unpack_from('>I', data, at)
        if flags != d['flags']:
            raise SystemExit('the flags word is not where it was expected')
        struct.pack_into('>I', data, at, flags & ~CS_RUNTIME)
        print('CodeDirectory at %#x (hash type %d): flags %#x -> %#x' % (d['at'], d['hash_type'], flags, flags & ~CS_RUNTIME))
    Path(argv[2]).write_bytes(data)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
