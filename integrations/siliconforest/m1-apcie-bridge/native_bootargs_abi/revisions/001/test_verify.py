"""Original synthetic parser cases, compiled Python mutants, explicit cached KC check."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import types
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'verify_kernel.py'
spec = importlib.util.spec_from_file_location('checked_abi', SOURCE)
VERIFIER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(VERIFIER)
UUID = bytes(range(16))
CODE = bytes((i * 11 + 3) % 256 for i in range(64))


def name(s):
    return s.encode().ljust(16, b'\0')


def section(n, vm, count, off):
    return name(n) + name('__TEXT_EXEC') + struct.pack('<2Q8I', vm, count, off, 2, 0, 0, 0x80000400, 0, 0, 0)


def segment(n, vm, size, off, sections=()):
    return struct.pack('<II', 0x19, 72 + 80 * len(sections)) + name(n) + struct.pack('<4Q4I', vm, size, off, size, 5, 5, len(sections), 0) + b''.join(sections)


def header(kind, commands):
    return struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, kind, len(commands), sum(map(len, commands)), 1, 0) + b''.join(commands)


def fixture(delta=0):
    member, codeoff = 512 + delta, 1024 + delta
    entry_name = b'com.apple.kernel\0'
    length = (32 + len(entry_name) + 7) & ~7
    entry = struct.pack('<II2Q2I', 0x80000035, length, 0x100000, member, 32, 0) + entry_name.ljust(length - 32, b'\0')
    outer = header(12, [segment('__TEXT', 0x100000, 512, member), segment('__TEXT_EXEC', 0x200000, 64, codeoff), entry])
    sections = [section('__text', 0x200000, 40, codeoff), section('__aux', 0x200028, 8, codeoff + 40)]
    embedded = header(2, [segment('__TEXT', 0x100000, 512, member), segment('__TEXT_EXEC', 0x200000, 64, codeoff, sections), struct.pack('<II', 0x1b, 24) + UUID])
    raw = bytearray(2048 + delta)
    raw[:len(outer)] = outer; raw[member:member + len(embedded)] = embedded; raw[codeoff:codeoff + 64] = CODE
    # Synthetic profile built independently, not from the parser under test.
    def sm(n, va, size):
        return dict(name=n, segment='__TEXT_EXEC', vmaddr=va, bytes=size, alignment=2,
                    flags=0x80000400, reserved=[0, 0, 0], reloff=0, nreloc=0)
    profile = dict(wire_profile='SYNTHETIC_TEST_ONLY', cpu=0x100000c, subtype=0, member_flags=1,
                   kernel_entry_vmaddr=0x100000, uuid='00010203-0405-0607-0809-0a0b0c0d0e0f',
                   executable=dict(name='__TEXT_EXEC', vmaddr=0x200000, vmsize=64, filesize=64,
                                   maxprot=5, initprot=5, flags=0, sections=[sm('__text', 0x200000, 40), sm('__aux', 0x200028, 8)]),
                   segment_sha256=hashlib.sha256(CODE).hexdigest(), text_sha256=hashlib.sha256(CODE[:40]).hexdigest())
    return bytes(raw), profile


def alter(raw, offset, fmt, *values):
    b = bytearray(raw); struct.pack_into(fmt, b, offset, *values); return bytes(b)


class ParserTests(unittest.TestCase):
    verifier = VERIFIER

    def check(self, raw, profile):
        try: return self.verifier._verify(raw, profile)
        except self.verifier.VerificationError as error: self.fail("known-valid synthetic image refused: " + str(error))

    def refused(self, raw, profile):
        with self.assertRaises(self.verifier.VerificationError):
            self.verifier._verify(raw, profile)

    def test_valid_relocation(self):
        a, p = fixture(); b, q = fixture(256)
        self.assertEqual(p, q)
        x, y = self.check(a, p), self.check(b, q)
        self.assertEqual(x['wire_profile'], 'SYNTHETIC_TEST_ONLY')
        self.assertEqual(x['segment_sha256'], y['segment_sha256'])
        self.assertNotEqual(x['image_sha256'], y['image_sha256'])
        self.assertEqual(y['kernel_header_fileoff'], 768)
        self.assertFalse(x['runtime_or_boot_admitted'])
        # Unconsumed trailing bytes are outside this code-profile scope.
        self.check(a[:-1] + b'X', p)

    def test_image_command_boundaries(self):
        raw, p = fixture()
        for n in range(1088):
            with self.subTest(truncate=n): self.refused(raw[:n], p)
        for data in [bytearray(raw), memoryview(raw), None, 'bytes']:
            self.refused(data, p)
        for offset, value in [(0, 0), (4, 7), (8, 1), (12, 2), (16, 0), (16, 4097), (20, (1 << 20) + 1), (28, 1),
                              (512, 0), (516, 7), (520, 1), (524, 12), (528, 4097), (532, (1 << 20) + 1), (540, 1),
                              (36, 0), (36, 7), (36, 73), (36, 0xfffffff8), (32 + 64, 257)]:
            with self.subTest(offset=offset, value=value): self.refused(alter(raw, offset, '<I', value), p)
        # Claimed commands must exactly fill sizeofcmds.
        self.refused(alter(raw, 16, '<I', 2), p)
        self.refused(alter(raw, 20, '<I', 208), p)

    def test_private_parser_budgets(self):
        # Shape-valid unknown commands isolate the parser's count limit.
        commands = [struct.pack('<II', 1, 8)] * 4097
        with self.assertRaises(self.verifier.VerificationError):
            self.verifier._header(header(12, commands), 0)
        with self.assertRaises(self.verifier.VerificationError):
            self.verifier._header(header(12, []), 0)

    def test_entry_and_strings(self):
        raw, p = fixture(); entry = 176
        for off, fmt, val in [(entry + 24, '<I', 31), (entry + 24, '<I', 56), (entry + 28, '<I', 1),
                              (entry + 16, '<Q', (1 << 64) - 1), (entry + 16, '<Q', 513),
                              (entry + 16, '<Q', 0), (entry + 8, '<Q', 0x100008)]:
            self.refused(alter(raw, off, fmt, val), p)
        for index in [entry + 32, entry + 48, entry + 55]:
            b = bytearray(raw); b[index] = 0xff; self.refused(bytes(b), p)
        b = bytearray(raw); b[entry + 32:entry + 56] = b'A' * 24; self.refused(bytes(b), p)
        # Duplicate kernel entry is rejected even with a distinct header offset.
        duplicate = bytearray(raw[entry:entry + 56]); struct.pack_into('<Q', duplicate, 16, 1536)
        b = bytearray(raw); b[232:288] = duplicate
        struct.pack_into('<II', b, 16, 4, 256); self.refused(bytes(b), p)
        # Fixed segment string padding and non-ASCII names are not ignored.
        for index in [40, 47]:
            b = bytearray(raw); b[index] = 0xff; self.refused(bytes(b), p)

    def test_layout_identity(self):
        raw, p = fixture(); seg = 512 + 32 + 72; sec = seg + 72
        uuid_offset = 512 + 32 + 72 + 232
        for off, fmt, value in [(536, '<I', 3), (seg + 24, '<Q', 0x200008), (seg + 32, '<Q', 65),
                                (seg + 48, '<Q', 63), (seg + 56, '<I', 7), (seg + 60, '<I', 1),
                                (seg + 68, '<I', 1), (sec + 32, '<Q', 0x200004), (sec + 40, '<Q', 39),
                                (sec + 52, '<I', 3), (sec + 56, '<I', 8), (sec + 60, '<I', 1),
                                (sec + 64, '<I', 0), (sec + 68, '<I', 1), (sec + 40, '<Q', (1 << 64) - 1)]:
            self.refused(alter(raw, off, fmt, value), p)
        b = bytearray(raw); b[uuid_offset + 8] ^= 1; self.refused(bytes(b), p)
        self.refused(alter(raw, uuid_offset + 4, '<I', 16), p)
        # Duplicate UUID, adjusted embedded command count/region.
        b = bytearray(raw); b[872:896] = raw[uuid_offset:uuid_offset + 24]
        struct.pack_into('<II', b, 528, 4, 352); self.refused(bytes(b), p)
        bad = copy.deepcopy(p); bad['text_sha256'] = '0' * 64; self.refused(raw, bad)

    def test_ranges_and_outer_mapping(self):
        raw, p = fixture(); seg = 616; sec = seg + 72
        for off, value in [(32 + 24, (1 << 64) - 16), (32 + 40, (1 << 64) - 1),
                           (seg + 40, (1 << 64) - 1), (seg + 48, (1 << 64) - 1)]:
            self.refused(alter(raw, off, '<Q', value), p)
        self.refused(alter(raw, sec + 48, '<I', 0xffffffff), p)
        # Member points to a golden blob while the outer loader maps corrupted bytes.
        b = bytearray(raw); b[1024] ^= 1; b[1792:1856] = CODE
        struct.pack_into('<Q', b, seg + 40, 1792)
        struct.pack_into('<I', b, sec + 48, 1792); struct.pack_into('<I', b, sec + 80 + 48, 1832)
        self.refused(bytes(b), p)
        # Double-adding member base cannot be hidden with a second golden copy.
        b = bytearray(raw); b[1536:1600] = CODE
        struct.pack_into('<Q', b, seg + 40, 1536)
        struct.pack_into('<I', b, sec + 48, 1536); struct.pack_into('<I', b, sec + 80 + 48, 1576)
        self.refused(bytes(b), p)
        # Outer mapping drift, without member drift.
        self.refused(alter(raw, 104 + 40, '<Q', 1536), p)
        # Loader covers the sections but not trailing segment bytes.
        self.refused(alter(raw, 104 + 48, '<Q', 48), p)
        # A partial overlapping mapping makes the text section ambiguous even
        # though only the original segment covers the complete64-byte range.
        b = bytearray(raw); b[232:304] = segment('__SHADOW', 0x200000, 40, 1280)
        struct.pack_into('<II', b, 16, 4, 272); self.refused(bytes(b), p)
        # Section relative/absolute mismatch and ambiguous outer VM coverage.
        self.refused(alter(raw, sec + 48, '<I', 512), p)
        b = bytearray(raw); b[232:304] = raw[104:176]
        struct.pack_into('<II', b, 16, 4, 272); self.refused(bytes(b), p)

    def test_full_bytes(self):
        raw, p = fixture()
        for i in range(64):
            b = bytearray(raw); b[1024 + i] ^= 1
            with self.subTest(byte=i): self.refused(bytes(b), p)

    def test_production_profile_is_not_injected(self):
        raw, p = fixture()
        with self.assertRaises(self.verifier.VerificationError): self.verifier.verify_kernel(raw)
        with self.assertRaises(TypeError): self.verifier.verify_kernel(raw, profile=p)
        old = self.verifier.__file__
        with tempfile.TemporaryDirectory() as tmp:
            self.verifier.__file__ = str(Path(tmp) / 'verify_kernel.py')
            try:
                with self.assertRaises(self.verifier.VerificationError): self.verifier.verify_kernel(raw)
                (Path(tmp) / 'PROFILE.json').write_text(json.dumps(p))
                with self.assertRaises(self.verifier.VerificationError): self.verifier.verify_kernel(raw)
            finally: self.verifier.__file__ = old


MUTATIONS = {
    'skip-arm64': ("cpu == 0x100000c and reserved == 0", "True and reserved == 0"),
    'skip-fileset-type': ("outer['filetype'] == 12 and", 'True and'),
    'skip-member-type': ("member['filetype'] == 2 and", 'True and'),
    'skip-subtype': ("member['subtype'] == profile['subtype'] and", 'True and'),
    'skip-uuid': ("member['uuids'] == [profile['uuid']]", 'True'),
    'skip-layout': ("_normalized(executable) == profile['executable']", 'True'),
    'skip-outer-executable': ("_mapped(outer, executable['vmaddr'], executable['filesize']) == executable['fileoff']", 'True'),
    'skip-outer-sections': ("_mapped(outer, section['vmaddr'], section['bytes']) == section['fileoff']", 'True'),
    'skip-segment-hash': ("segment_digest == profile['segment_sha256']", 'True'),
    'skip-text-hash': ("text_digest == profile['text_sha256']", 'True'),
    'double-offset': ("raw[executable['fileoff']:executable['fileoff'] + executable['filesize']]", "raw[entry['fileoff'] + executable['fileoff']:entry['fileoff'] + executable['fileoff'] + executable['filesize']]"),
    'missing-command-budget': ('0 < count <= 4096 and size <= 1 << 20', '0 < count <= 0xffffffff and size <= 1 << 20'),
    'missing-exact-fill': ("_need(at == end, 'exact load-command fill')", "_need(True, 'exact load-command fill')"),
    'profile-hash-bypass': ('hashlib.sha256(raw).hexdigest() == _PROFILE_SHA256', 'True'),
}


def suite(module):
    cls = type('BoundParserTests', (ParserTests,), {'verifier': module})
    return unittest.defaultTestLoader.loadTestsFromTestCase(cls)


def mutations():
    original = SOURCE.read_text(); killed = []
    for label, (before, after) in MUTATIONS.items():
        if original.count(before) != 1: raise AssertionError('mutation anchor ' + label)
        code = compile(original.replace(before, after), str(SOURCE), 'exec')
        module = types.ModuleType('mutant_' + label); module.__file__ = str(SOURCE)
        exec(code, module.__dict__)
        result = unittest.TestResult(); suite(module).run(result)
        if result.errors or not result.failures:
            raise AssertionError('not an assertion-killed compiled Python mutant ' + label + ': ' + repr(result.errors))
        killed.append(label)
    return killed


def actual_cached(path):
    # Explicit request: missing/unreadable/oversized is an error, never skip.
    with path.open("rb") as stream:
        raw = stream.read((256 << 20) + 1)
    if len(raw) > 256 << 20: raise ValueError("cached image exceeds bound")
    profile = json.loads((HERE / 'PROFILE.json').read_text())
    if hashlib.sha256(raw).hexdigest() != profile['selected_KC']['sha256']:
        raise ValueError('explicit cached-artifact check requires the pinned selected KC')
    result = VERIFIER.verify_kernel(raw)
    for off in (result['segment_fileoff'], result['segment_fileoff'] + profile['executable']['filesize'] - 1):
        changed = bytearray(raw); changed[off] ^= 1
        try: VERIFIER.verify_kernel(bytes(changed))
        except VERIFIER.VerificationError: pass
        else: raise AssertionError('actual code corruption accepted')
    return {'status': 'ACTUAL_CACHED_KC_PROFILE_CHECK_ONLY', 'path': str(path), 'verification': result,
            'in_memory_actual_segment_first_last_bit_corruptions_refused': True}


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutations', action='store_true')
    parser.add_argument('--actual-kc', type=Path); args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(suite(VERIFIER))
    if not result.wasSuccessful(): return 1
    record = {'synthetic_test_groups': result.testsRun, 'synthetic_scope': 'PRIVATE_PROFILE_ONLY_NOT_ACTUAL_KC'}
    if args.mutations: record['compiled_Python_assertion_killed_mutants'] = mutations()
    if args.actual_kc is not None:
        try: record['actual_cached_artifact'] = actual_cached(args.actual_kc)
        except (OSError, ValueError) as error:
            print(json.dumps({'status': 'ERROR_REQUESTED_ACTUAL_CHECK', 'error': str(error)})); return 2
    else: record['actual_cached_artifact'] = {'status': 'NOT_REQUESTED_NOT_EVALUATED'}
    print(json.dumps(record, indent=2)); return 0


if __name__ == '__main__': raise SystemExit(main())
