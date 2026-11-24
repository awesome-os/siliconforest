#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host-only checks of the native-boot launcher pieces; no hardware.
    python3 integrations/siliconforest/m1-native-boot/test_native_boot.py -v"""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import native_boot
import native_stub


class Proxy:
    P_VECTOR = 'P_VECTOR'

    def __init__(self, log, stub=0x80a000000):
        self.log, self.stub, self.mem = log, stub, {}

    def memalign(self, align, size):
        self.log.append(('memalign', align, size))
        return self.stub

    def __getattr__(self, name):
        def call(*args):
            self.log.append((name,) + args)
            if name == 'read32':
                return self.mem.get(args[0], 0)
            if name == 'write32':
                self.mem[args[0]] = args[1]
        return call


class Console:
    def __init__(self, port, log):
        log.append(('console', port))
        self.done_seen, self.proof = True, SimpleNamespace(output=b'x')
        self.dev = SimpleNamespace(write=lambda data: len(data), flush=lambda: None)
        self.thread = threading.Thread(target=lambda: None)
        self.log = log

    def close(self):
        self.log.append(('console_close',))


class NativeBoot(unittest.TestCase):
    def test_modes(self):
        self.assertFalse(native_boot.Mode(''))
        self.assertFalse(native_boot.Mode('off'))
        self.assertTrue(native_boot.Mode('probe').probe)
        mode = native_boot.Mode('native wdt=130 nosprr')
        self.assertTrue(mode.native and mode.wdt == 130 and not mode.sprr and mode.amx)
        self.assertTrue(native_boot.Mode('native vdm-return').vdm_return)
        for bad in ('native', 'probe native vdm-return', 'probe wdt=100', 'native wdt=171',
                    'native wdt=20', 'native vdm-return fast', 'probe vdm-return'):
            with self.assertRaises(ValueError, msg=bad):
                native_boot.Mode(bad)

    def test_stub_is_the_assembled_source(self):
        blob = bytes.fromhex(native_stub.STUB_HEX)
        self.assertEqual(hashlib.sha256(blob).hexdigest(), native_stub.STUB_SHA256)
        self.assertEqual(len(blob), 0x4000)
        # two branches at the two entries; every vector slot starts with mov x17,#n; mov x18,#level
        for offset in (0, 4, 8):
            self.assertEqual(struct.unpack_from('<I', blob, offset)[0] >> 26, 0b000101)
        for base, level in ((0x800, 2), (0x1000, 1)):
            for n in range(16):
                first, second = struct.unpack_from('<II', blob, base + 0x80 * n)
                self.assertEqual(first, 0xd2800011 | n << 5)
                self.assertEqual(second, 0xd2800012 | level << 5)
        if shutil.which('clang') and shutil.which('llvm-objcopy'):
            subprocess.run([str(HERE/'stub/build.sh'), '--check'], check=True, capture_output=True)

    def test_stub_parameters(self):
        blob = native_boot.stub_image(0x235200000, 3, 0x855520000)
        self.assertEqual(struct.unpack_from('<5sxxxQQQ', blob, native_boot.PARAMS),
                         (b'PDNB1', 0x235200000, 3, 0x855520000))

    def start(self, mode, stub=0x80a000000, phys_base=0x852ebc000):
        log = []
        proxy = Proxy(log, stub)
        blob = native_boot.stub_image(0x235200000, 3)
        iface = SimpleNamespace(writemem=lambda a, d: log.append(('writemem', a, len(d))),
                                readmem=lambda a, n: blob)
        backend = SimpleNamespace(p=proxy, iface=iface, ba=SimpleNamespace(phys_base=0x802078000),
                                  snapshot=dict(uart0=dict(translated_addr=0x235200000)))
        wdt = SimpleNamespace(get_reg=lambda n: (native_boot.T8103_WDT, 0x4000))
        hv = SimpleNamespace(entry=0x85551e490, guest_base=0x853f74000, bootargs_off=0x3780000 - 0x4000,
                             phys_base=phys_base, adt={'/arm-io/wdt': wdt})
        layout = dict(entry=hv.entry, phys_base=hv.phys_base)
        saved = {}
        with tempfile.TemporaryDirectory() as directory:
            native_boot.start(native_boot.Mode(mode), hv, backend, layout, Path(directory), 5,
                              lambda port: Console(port, log),
                              lambda d, name, value: saved.__setitem__(name, value))
        return log, saved, hv

    def test_start_order(self):
        log, saved, hv = self.start('native vdm-return')
        names = [entry[0] for entry in log]
        self.assertLess(names.index('writemem'), names.index('console'))
        self.assertLess(names.index('console'), names.index('request'))
        self.assertEqual(names[-1], 'console_close')
        self.assertNotIn('write32', names)                      # no watchdog unless asked for
        self.assertNotIn('hv_init', names)
        self.assertNotIn('hv_start', names)
        request = log[names.index('request')]
        self.assertEqual(request[1:], ('P_VECTOR', 0x80a000004, hv.entry, hv.guest_base + hv.bootargs_off, 0, 0, 0))
        self.assertEqual(log[names.index('console')], ('console', '/dev/cu.debug-console'))
        self.assertIs(saved['native-start.json']['hypervisor'], False)
        self.assertTrue(saved['capture-core.json']['console_capture_finalized'])

    def test_watchdog_is_armed_before_the_vector(self):
        log, saved, _ = self.start('native wdt=130')
        names = [entry[0] for entry in log]
        writes = [entry[1:] for entry in log if entry[0] == 'write32']
        base = native_boot.T8103_WDT
        self.assertEqual(writes, [(base + 0x1c, 0), (base + 0x10, 0), (base + 0x14, 130 * 24_000_000), (base + 0x1c, 4)])
        self.assertLess(max(i for i, n in enumerate(names) if n == 'write32'), names.index('request'))
        self.assertEqual(saved['native-start.json']['watchdog_seconds'], 130)

    def test_typing_is_paced(self):
        chunks = []
        dev = SimpleNamespace(write=lambda data: chunks.append(bytes(data)) or len(data), flush=lambda: None, timeout=.1)
        paced = native_boot.PacedSerial(dev)
        paced.GAP = 0
        self.assertEqual(paced.write(b'x' * 20), 20)
        self.assertEqual([len(c) for c in chunks], [8, 8, 4])
        self.assertEqual(paced.timeout, .1)

    def test_stub_must_be_below_xnu_memory(self):
        with self.assertRaises(ValueError):
            self.start('native vdm-return', stub=0x852ebc000)

    def test_patch_applies_to_the_launcher_tree(self):
        lab = ROOT/'runs/m1-desktop-010/pcie-m2-host-001/lab'
        if not lab.is_dir() or not shutil.which('patch'):
            self.skipTest('no launcher tree here')
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory)/'lab'
            shutil.copytree(lab, copy, ignore=shutil.ignore_patterns('__pycache__'))
            subprocess.run(['patch', '-s', '-d', str(copy), '-p1', '-i', str(HERE/'launcher-native.patch')], check=True)
            for name in ('integrations/siliconforest/m1-bringup/host_progress.py', 'tools/m1_bringup_cycle.py'):
                compile((copy/name).read_text(), name, 'exec')
            # the frozen recovery controller and the hash-pinned helpers are not touched
            for name in ('tools/m1_kernel_cycle.py', 'integrations/siliconforest/m1-firstboot/host_console.py',
                         'integrations/siliconforest/m1-bringup/normal_image.py'):
                self.assertEqual((copy/name).read_bytes(), (lab/name).read_bytes())
            # it contains the boot-speed launcher patch unchanged
            fast = Path(directory)/'fast'
            shutil.copytree(lab, fast, ignore=shutil.ignore_patterns('__pycache__'))
            subprocess.run(['patch', '-s', '-d', str(fast), '-p1', '-i',
                            str(ROOT/'integrations/siliconforest/m1-boot-speed/launcher-fast-quiet.patch')], check=True)
            def lines(tree):
                text = (tree/'integrations/siliconforest/m1-bringup/host_progress.py').read_text()
                return [line.strip() for line in text.splitlines()]
            ours, theirs, position = lines(copy), lines(fast), 0
            for line in theirs:                # theirs is a subsequence of ours (indentation aside)
                position = ours.index(line, position) + 1


if __name__ == '__main__':
    unittest.main()
