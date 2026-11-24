#!/usr/bin/env python3
"""Check real ARM64 KC extents and the observed 16K contiguous-hint failure.

Requires actual input kernel/KC artifacts. This reads files and models page
attributes; it does not execute privileged code or establish a hardware boot.
"""
import argparse
import json
from pathlib import Path
import struct
import unittest

import normal_image as image

PAGE, HINT = 0x4000, 0x200000


def segments(raw, base=0):
    _, _, commands = image.commands(raw, base)
    out = {}
    for cmd, off, size in commands:
        if cmd != 0x19:
            continue
        image.require(size >= 72, 'Truncated segment')
        name = raw[off+8:off+24].split(b'\0')[0].decode('ascii')
        image.require(name not in out, 'Ambiguous segment')
        vm, nvm, fileoff, nfile = struct.unpack_from('<4Q', raw, off+24)
        out[name] = dict(vm=vm, size=nvm, fileoff=fileoff, filesize=nfile, command=off)
    return out


def kernel_member(raw):
    kind, _, commands = image.commands(raw)
    image.require(kind == 12, 'Not a collection')
    found = []
    for cmd, off, size in commands:
        if cmd == 0x80000035:
            vm, fileoff, nameoff, _ = struct.unpack_from('<QQII', raw, off+8)
            name = raw[off+nameoff:off+size].split(b'\0')[0]
            if name == b'com.apple.kernel':
                found.append(fileoff)
    image.require(len(found) == 1, 'Missing/ambiguous kernel member')
    return found[0]


def conflicts(text, data, slide):
    """TEXT RNX+HINT then DATA_CONST RWNX, as arm_vm_prot_init requests.

    A fully overwritten hint group is unhinted and uniform. A partial rewrite
    leaves a mixture of RO hinted and RW unhinted descriptors, which violates
    the contiguous-group contract. Report only groups affected by this pair.
    """
    start, end = text['vm']+slide, text['vm']+slide+text['size']
    dstart, dend = data['vm']+slide, data['vm']+slide+data['size']
    groups = []
    for group in range((start+HINT-1)//HINT*HINT, end-HINT+1, HINT):
        overlap = max(0, min(group+HINT, dend)-max(group, dstart))
        if 0 < overlap < HINT:
            groups.append(dict(address=group, hinted_RO_pages=(HINT-overlap)//PAGE,
                               unhinted_RW_pages=overlap//PAGE))
    return groups


class LayoutChecks(unittest.TestCase):
    def test_fixed_embedded_text_matches_linker_and_preserves_bss(self):
        original = segments(self.kernel)
        fixed = segments(self.fixed, kernel_member(self.fixed))
        self.assertEqual(fixed['__TEXT']['vm'], original['__TEXT']['vm'])
        self.assertEqual(fixed['__TEXT']['size'], original['__TEXT']['size'])
        self.assertEqual(fixed['__TEXT']['filesize'], original['__TEXT']['filesize'])
        self.assertLessEqual(fixed['__TEXT']['vm']+fixed['__TEXT']['size'], fixed['__DATA_CONST']['vm'])
        self.assertGreater(original['__DATA']['size'], original['__DATA']['filesize'])
        self.assertEqual(fixed['__DATA']['size'], original['__DATA']['size'])

    def test_observed_slides_and_all_2048_phases(self):
        old = segments(self.old, kernel_member(self.old))
        fixed = segments(self.fixed, kernel_member(self.fixed))
        self.assertEqual(conflicts(old['__TEXT'], old['__DATA_CONST'], 0xbe2c000), [])
        failed = conflicts(old['__TEXT'], old['__DATA_CONST'], 0xc790000)
        self.assertEqual(failed, [dict(address=0xfffffe0013800000, hinted_RO_pages=68, unhinted_RW_pages=60)])
        before, after = 0, 0
        for slide in range(0, 32 << 20, PAGE):
            before += bool(conflicts(old['__TEXT'], old['__DATA_CONST'], slide))
            after += bool(conflicts(fixed['__TEXT'], fixed['__DATA_CONST'], slide))
        self.assertEqual((before, after), (1232, 0))

    def test_no_image_body_changed(self):
        self.assertEqual(len(self.fixed), len(self.old))
        # The source fix can affect __TEXT.vmsize in both the copied outer
        # header and embedded kernel header; no file bytes or other fields.
        allowed = set()
        kernel_text_vm = segments(self.kernel)['__TEXT']['vm']
        for base in (0, kernel_member(self.old)):
            # The outer collection also has its own __TEXT header segment at
            # KC_LINK, alongside its copy of the kernel's __TEXT command.
            for cmd, off, size in image.commands(self.old,base)[2]:
                if (cmd == 0x19 and size >= 72 and
                    self.old[off+8:off+24].rstrip(b'\0') == b'__TEXT' and
                    struct.unpack_from('<Q',self.old,off+24)[0] == kernel_text_vm):
                    allowed.update(range(off+32,off+40))
        changed = {i for i,(a,b) in enumerate(zip(self.old,self.fixed)) if a != b}
        self.assertTrue(changed)
        self.assertTrue(changed <= allowed, sorted(changed-allowed)[:20])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--old-collection', type=Path, required=True)
    parser.add_argument('--fixed-collection', type=Path, required=True)
    args = parser.parse_args()
    LayoutChecks.kernel = image.bounded_file(args.kernel)
    LayoutChecks.old = image.bounded_file(args.old_collection)
    LayoutChecks.fixed = image.bounded_file(args.fixed_collection)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(LayoutChecks))
    print(json.dumps(dict(status='HOST_LAYOUT_CHECKS_PASSED' if result.wasSuccessful() else 'HOST_LAYOUT_CHECKS_FAILED',
                          tests_run=result.testsRun, kernel_sha256=image.sha(LayoutChecks.kernel),
                          old_collection_sha256=image.sha(LayoutChecks.old), fixed_collection_sha256=image.sha(LayoutChecks.fixed),
                          phase_count=2048, hardware_accessed=False, command_line_observed=False)))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    raise SystemExit(main())
