#!/usr/bin/env python3
"""Host/saved-ADT tests; never imports a hardware transport."""
import ast
import ctypes as C
import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
spec = importlib.util.spec_from_file_location('gpu_preflight_decoder', HERE / 'decode.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class Board(C.Structure):
    _fields_ = [('base', C.c_uint64), ('size', C.c_uint32), ('offset', C.c_uint32)]


GET = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.c_char_p,
                  C.POINTER(C.c_void_p), C.POINTER(C.c_size_t))


def fixture():
    reg = struct.pack('<QQ', 0x3b700000, 0x8c000) + bytes(53 * 16)
    devices = bytearray(4 * 48)
    for i, (id_, parent, flags, name) in enumerate([
        (99, 0, 0xa2, b'GFX'), (314, 99, 0x10, b'GFX-SGX'),
        (315, 0, 0x10, b'GFX-BUSY'), (316, 99, 0x10, b'GFX-ASC')
    ]):
        off = i * 48; devices[off] = flags
        struct.pack_into('<H', devices, off + 4, parent)
        struct.pack_into('<H', devices, off + 26, id_)
        devices[off + 32:off + 33 + len(name)] = name + b'\0'
        devices[off + 10] = 127 if i == 0 else 255
        devices[off + 11] = 0 if i == 0 else 255  # Virtual placeholders MUST be ignored.
    return {
        ('/', 'target-type'): b'J293\0',
        ('/', 'compatible'): b'J293AP\0MacBookPro17,1\0AppleARM\0',
        ('/arm-io', 'compatible'): b'arm-io,t8103\0',
        ('/arm-io', 'ranges'): struct.pack('<QQQ', 0, 0x200000000, 0x100000000),
        ('/arm-io/sgx', 'compatible'): b'gpu,t8103\0',
        ('/arm-io/sgx', 'reg'): struct.pack('<QQQQ', 0x4000000, 0x4000000, 0x4d00000, 0x20000),
        ('/arm-io/sgx', 'clock-gates'): struct.pack('<II', 314, 315),
        ('/arm-io/gfx-asc', 'compatible'): b'iop,ascwrap-v4\0',
        ('/arm-io/gfx-asc', 'reg'): struct.pack('<QQQQ', 0x6400000, 0x6c000, 0x6050000, 8),
        ('/arm-io/gfx-asc', 'clock-gates'): struct.pack('<I', 316),
        ('/arm-io/gfx-asc', 'power-gates'): struct.pack('<I', 316),
        ('/arm-io/pmgr', 'compatible'): b'pmgr1,t8103\0',
        ('/arm-io/pmgr', 'devices'): bytes(devices),
        ('/arm-io/pmgr', 'reg'): reg,
        ('/arm-io/pmgr', 'ps-regs'): struct.pack('<III', 0, 0, 0),
    }


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='gpu-preflight-')
        cls.d = Path(cls.tmp.name)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-O1', '-g', '-fsanitize=undefined', '-shared', '-fPIC',
                        str(HERE / 'board.c'), '-o', str(cls.d / 'board.so')], check=True)
        cls.lib = C.CDLL(str(cls.d / 'board.so'))
        cls.lib.gpf_validate_board.argtypes = [GET, C.c_void_p, C.POINTER(Board)]

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def validate(self, props):
        buffers = {k: C.create_string_buffer(v) for k, v in props.items()}
        @GET
        def get(_, path, key, ptr, count):
            k = (path.decode(), key.decode())
            if k not in buffers: return -1
            ptr[0] = C.addressof(buffers[k]); count[0] = len(props[k]); return 0
        out = Board(); C.memset(C.byref(out), 0xa5, C.sizeof(out))
        result = self.lib.gpf_validate_board(get, None, C.byref(out))
        return result, out

    def test_synthetic_exact_board_virtual_domains_and_physical_GFX(self):
        result, b = self.validate(fixture())
        self.assertEqual(result, 0)
        self.assertEqual((b.base, b.size, b.offset), (0x23b700000, 0x4000, 0x3f8))

    def test_missing_truncated_ambiguous_or_wrong_board_refuses_unchanged(self):
        p = fixture()
        for key, value in p.items():
            for changed in [None, value[:-1]]:
                q = dict(p)
                if changed is None: q.pop(key)
                else: q[key] = changed
                result, out = self.validate(q)
                self.assertEqual(result, 1, (key, changed))
                self.assertEqual(bytes(out), b'\xa5' * C.sizeof(out))
        for off in [0, 4, 26, 32, 48, 48 + 4]:
            q = fixture(); d = bytearray(q['/arm-io/pmgr', 'devices']); d[off] ^= 1
            q['/arm-io/pmgr', 'devices'] = bytes(d)
            self.assertEqual(self.validate(q)[0], 1)
        q = fixture(); q['/arm-io/pmgr', 'devices'] *= 2
        self.assertEqual(self.validate(q)[0], 1)
        q = fixture(); q['/arm-io', 'ranges'] += struct.pack('<QQQ', 0, 0x300000000, 0x100000000)
        self.assertEqual(self.validate(q)[0], 1)

    def test_actual_saved_guest_ADT_without_transport_import(self):
        path = LAB / 'runs/m1-desktop-010/dart-survey-attempt-001/guest/guest-adt.bin'
        if not path.exists(): self.skipTest('private saved guest ADT absent')
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), '8f8613d8787f912137b482991dc0d5eca403d93e01bceb212019a02be9b8d8c5')
        source = LAB / 'integrations/puredarwin/m1-dart-survey/test_survey.py'
        tree = ast.parse(source.read_text())
        function = next(x for x in tree.body if isinstance(x, ast.FunctionDef) and x.name == 'parse_adt')
        env = {'struct': struct}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(source), 'exec'), env)
        nodes = env['parse_adt'](raw)
        props = {(p, k): v for p, d in nodes.items() for k, v in d.items()}
        self.assertEqual(self.validate(props)[0], 0)

    def test_observer_under_ASan_UBSan_and_ndebug_refusal(self):
        args = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-O1', '-g',
                '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
                str(HERE / 'observe.c'), str(HERE / 'test_observe.c'), '-o', str(self.d / 'observe')]
        subprocess.run(args, check=True)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        subprocess.run([str(self.d / 'observe')], check=True, env=env)
        bad = subprocess.run(args[:1] + ['-DNDEBUG'] + args[1:], capture_output=True, text=True)
        self.assertNotEqual(bad.returncode, 0); self.assertIn('NDEBUG forbidden', bad.stderr)

    def test_strict_raw_decoder_no_false_GPU_clearance(self):
        nonce = 'a' * 48
        def block(w):
            b = struct.pack('<16I', *w)
            return (f'DL25_GPU_PREFLIGHT_BEGIN_{nonce} bytes=64\n'
                    f'DL25_GPU_PREFLIGHT_HEX_{nonce} index=0 data={b.hex()}\n'
                    f'DL25_GPU_PREFLIGHT_END_{nonce} sha256={hashlib.sha256(b).hexdigest()}\n').encode()
        good = [1, 64, 0, 1, 1, 1, 0xff, 0, 314, 315, 316, 7, 99, 0x3f8, 0, 0]
        self.assertEqual(decoder.decode(block(good), nonce)['status'], 'PMGR_OBSERVED_GPU_ACCESS_NOT_QUALIFIED')
        self.assertFalse(decoder.decode(block(good), nonce)['gpu_mmio_authorized'])
        refused = [1, 64, 1] + [0] * 13
        self.assertEqual(decoder.decode(block(refused), nonce)['status'], 'BOARD_REFUSED')
        failed = good.copy(); failed[2] = 2; failed[5] = failed[6] = 0
        self.assertEqual(decoder.decode(block(failed), nonce)['status'], 'PMGR_READ_FAILED')
        for i in range(16):
            if i == 6: continue  # Any observed numeric power value is valid data, never clearance.
            changed = good.copy(); changed[i] ^= 1
            with self.assertRaises(ValueError): decoder.decode(block(changed), nonce)
        for b in [block(good)[:-4], block(good) + b'extra\n', block(good).replace(b'index=0', b'index=1')]:
            with self.assertRaises(ValueError): decoder.decode(b, nonce)

    def test_native_source_access_and_preservation_scope(self):
        cpp = (HERE / 'PDGPUPreflight.cpp').read_text()
        self.assertLess(cpp.index('gpf_validate_board('), cpp.index('result=gpf_observe('))
        self.assertLess(cpp.index('releaseMap();', cpp.index('bool PDGPUPreflight::start')),
                        cpp.index('provider->setProperty('))
        self.assertIn('off!=0x3f8', cpp)
        self.assertIn('kIOMapReadOnly', cpp)
        self.assertIn('gGPUPreflightClaimed', cpp)
        self.assertIn('provider->setProperty("PDGPUPreflightV1",data)', cpp)
        self.assertNotRegex(cpp, r'\b(?:write32|mask32|set32|clear32|pcie_init|dart_init|IOConnectCall|IOSleep)\s*\(')
        collect = (HERE / 'collect.c').read_text()
        self.assertIn('io_registry_entry_get_property_bytes(entry,key,(char *)raw,&count)', collect)
        self.assertNotRegex(collect, r'IOServiceOpen|IOConnect|IOMemory|/dev/')


if __name__ == '__main__': unittest.main(verbosity=2)
