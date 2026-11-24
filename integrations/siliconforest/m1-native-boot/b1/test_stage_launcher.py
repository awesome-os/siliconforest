#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Dry run of the launcher's `stage` mode with fakes; no hardware, no controller.
    python3 -B integrations/siliconforest/m1-native-boot/b1/test_stage_launcher.py -v"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import native_boot
import native_stage
import pdsf_pack

BASE, PHYS, MEM, OLD_TOP = 0x8_1234_0000, 0x8_0207_8000, 0x3_d000_0000, 0x8_0a00_0000


def fake_m1n1(size=0x8000):
    blob = bytearray(os.urandom(size))
    blob[0x808:0x810] = b'AURALOAD'
    struct.pack_into('<Q', blob, 0x818, size)
    return bytes(blob)


class Fakes:
    def __init__(self, usb=1):
        self.log, self.mem = [], {}
        outer = self

        class Proxy:
            P_VECTOR = 'P_VECTOR'

            def iodev_whoami(self):
                return 2 + usb

            def __getattr__(self, name):
                return lambda *args: outer.log.append((name,) + args)

        def writemem(address, data):
            self.log.append(('writemem', address, len(data)))
            for i in range(0, len(data), 0x4000):          # page-granular store
                self.mem[address + i] = bytes(data[i:i + 0x4000])

        def readmem(address, size):
            self.log.append(('readmem', address, size))
            return b''.join(self.mem[a] for a in range(address, address + size, 0x4000))[:size]

        heap = SimpleNamespace(memalign=lambda align, size: self.log.append(('memalign', align, size)) or BASE)
        self.raw_bootargs = struct.pack('<HHIQQQQ', 2, 2, 0, 0xfffffe0010000000, PHYS, MEM, OLD_TOP) + os.urandom(700)
        self.backend = SimpleNamespace(
            p=Proxy(), iface=SimpleNamespace(writemem=writemem, readmem=readmem), u=SimpleNamespace(heap=heap),
            ba=SimpleNamespace(phys_base=PHYS, mem_size=MEM, top_of_kernel_data=OLD_TOP),
            proxy_module=SimpleNamespace(IODEV=SimpleNamespace(USB0=2)))
        self.inputs = SimpleNamespace(contents=dict(kernel=os.urandom(90000), ramdisk=bytes(1 << 20) + os.urandom(3000)))
        self.saved = {}
        self.layout = dict(phys_base=0x8_52eb_c000, guest_base=0x8_53f7_4000, bootargs_off=0x377c000, entry=0x8_5551_e490)

    def console(self, port):
        self.log.append(('console', port))
        return SimpleNamespace(done_seen=True, proof=SimpleNamespace(output=b'x'),
                               dev=SimpleNamespace(write=len, flush=lambda: None),
                               thread=threading.Thread(target=lambda: None),
                               close=lambda: self.log.append(('console_close',)))

    def run(self, words, bootargs='-v -s serial=3 rd=md0 cpus=8 sfcpuheld=1 '):
        native = native_boot.Mode(words)
        with tempfile.TemporaryDirectory() as directory:
            native_stage.start(native, self.backend, self.inputs, native_boot.bootargs(native, bootargs),
                               self.raw_bootargs, self.layout, Path(directory), 5, self.console,
                               lambda d, name, value: self.saved.__setitem__(name, value))
        return [entry[0] for entry in self.log]


class StageLauncher(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.m1n1 = fake_m1n1()
        (Path(self.tmp.name) / native_stage.IMAGE).write_bytes(self.m1n1)
        self.here, native_stage.HERE = native_stage.HERE, Path(self.tmp.name)

    def tearDown(self):
        native_stage.HERE = self.here
        self.tmp.cleanup()

    def uploaded(self, fakes, size):
        return b''.join(fakes.mem[a] for a in range(BASE, BASE + size, 0x4000))[:size]

    def test_one_upload_one_vector_then_silence(self):
        fakes = Fakes()
        names = fakes.run('native stage vdm-return noiolog')
        record = fakes.saved['native-start.json']
        vector = names.index('request')
        self.assertEqual(names.count('request'), 1)
        self.assertLess(max(i for i, n in enumerate(names) if n == 'writemem'), names.index('console'))
        self.assertLess(names.index('console'), vector)
        self.assertEqual(names[vector + 1:], ['console_close'])        # nothing over the proxy after the vector
        size = (record['image_bytes'] + 0x3fff) & ~0x3fff
        self.assertEqual(fakes.log[vector][1:], ('P_VECTOR', BASE + 0x800, BASE + size, 0, 0, 0, 0))
        self.assertEqual((record['base'], record['entry'], record['bootargs']), (BASE, BASE + 0x800, BASE + size))
        self.assertIs(record['hypervisor'], False)                      # what the controller's vdm-return reads
        self.assertTrue(record['stage'])
        for forbidden in ('hv_init', 'hv_start', 'smp_start_secondaries', 'write32', 'write64', 'nvme_init', 'pcie_init'):
            self.assertNotIn(forbidden, names)
        self.assertTrue(fakes.saved['capture-core.json']['console_capture_finalized'])
        # the record the controller's verdict binds the console to: the kernel in the image
        import hashlib
        self.assertEqual(fakes.saved['before-start.json']['image_sha256'],
                         hashlib.sha256(fakes.inputs.contents['kernel']).hexdigest())
        self.assertLess(list(fakes.saved).index('before-start.json'), list(fakes.saved).index('native-start.json'))

        image = self.uploaded(fakes, record['image_bytes'])
        self.assertTrue(image.startswith(self.m1n1))
        found, end = pdsf_pack.parse(image, len(self.m1n1))
        self.assertEqual([(k, bool(f & 1), ok) for k, f, _, _, ok, _ in found],
                         [(1, False, True), (2, True, True), (3, True, True)])
        self.assertEqual(image[end:], bytes(4))
        body = {k: image[o:o + s] for k, _, s, _, _, o in found}
        self.assertEqual(zlib.decompress(body[2], 31), fakes.inputs.contents['kernel'])
        self.assertEqual(zlib.decompress(body[3], 31), fakes.inputs.contents['ramdisk'])
        self.assertEqual(body[1].decode().splitlines(),
                         ['bootargs=-v -s serial=0x13 rd=md0 cpus=8 sfcpuheld=1 ', 'display=1', 'usb-nodes=remove1',
                          'sprr=1', 'amx=1', 'xnu-base=0x852ebc000'])
        self.assertEqual(record['config'], body[1].decode().splitlines())
        self.assertEqual(record['expected'], dict(xnu_base='0x852ebc000', entry='0x85551e490', bootargs='0x8576f0000'))

        # the boot arguments the stage is entered with: this m1n1's, the heap behind the image
        entered = fakes.mem[BASE + size]
        raw = fakes.raw_bootargs
        self.assertEqual(entered[:32] + entered[40:len(raw)], raw[:32] + raw[40:])
        self.assertEqual(struct.unpack_from('<Q', entered, 32)[0], BASE + size + 0x4000)
        self.assertEqual(entered[len(raw):], bytes(0x4000 - len(raw)))

    def test_boot_argument_addresses_wrap_as_64_bit(self):
        # the collected boot arguments of sfb05b and of sfb05c (virt_base above the address)
        self.assertEqual(native_stage.physical(0x3ec4000, 0xfffffffffe078000, 0x802078000), 0x807ec4000)
        self.assertEqual(native_stage.physical(0xfffffe0016520000, 0xfffffe0011c30000, 0x851c30000), 0x856520000)

    def test_words_reach_the_configuration(self):
        fakes = Fakes(usb=0)
        fakes.run('native stage wdt=150 nosprr usbkeep hostseed lowmem', bootargs='-v -s serial=3 rd=md0 cpus=1 ')
        record = fakes.saved['native-start.json']
        self.assertEqual(record['config'], ['bootargs=-v -s serial=3 rd=md0 cpus=1 ', 'display=1', 'usb-nodes=keep',
                                            'sprr=0', 'amx=1', 'wdt=150'])
        self.assertEqual(record['watchdog_seconds'], 150)
        self.assertIsNone(record['expected'])                           # lowmem: the stage chooses
        image = self.uploaded(fakes, record['image_bytes'])
        found, _ = pdsf_pack.parse(image, len(self.m1n1))
        text = image[found[0][5]:found[0][5] + found[0][2]].decode()
        seed = [line for line in text.splitlines() if line.startswith('seed-hex=')]
        self.assertEqual(len(seed), 1)
        self.assertEqual(len(bytes.fromhex(seed[0][9:])), 256)
        self.assertNotIn('seed-hex', ''.join(record['config']))        # never in the record or the log
        self.assertNotIn('write32', [entry[0] for entry in fakes.log])  # the stage arms the watchdog, not the launcher

    def test_no_ramdisk_image_for_a_disk_root(self):
        """noramdisk: stage, configuration and kernel only; the RAM root the launcher holds is not uploaded."""
        fakes = Fakes()
        disk = '-v -s serial=3 rd=uuid boot-uuid=0F0E0D0C-0000-4000-8000-00000000000A cpus=8 sfcpuheld=1 '
        names = fakes.run('native stage vdm-return noiolog noramdisk multiuser', bootargs=disk)
        record = fakes.saved['native-start.json']
        image = self.uploaded(fakes, record['image_bytes'])
        found, end = pdsf_pack.parse(image, len(self.m1n1))
        self.assertEqual([k for k, *_ in found], [1, 2])                # config, kernel: no RAM root record
        self.assertEqual([r['type'] for r in record['records']], ['config', 'kernel'])
        self.assertLess(record['image_bytes'], len(self.m1n1) + len(fakes.inputs.contents['kernel']) + 4096)
        config = image[found[0][5]:found[0][5] + found[0][2]].decode().splitlines()[0]
        self.assertIn(' rd=uuid boot-uuid=0F0E0D0C-0000-4000-8000-00000000000A ', config)
        self.assertNotIn('rd=md0', config)
        self.assertNotIn(' -s ', config)                                # multi-user
        self.assertEqual(names.count('request'), 1)
        # the same word with a RAM root on the command line is refused before anything is uploaded
        fakes = Fakes()
        with self.assertRaises(ValueError):
            fakes.run('native stage vdm-return noramdisk')
        self.assertNotIn('writemem', [entry[0] for entry in fakes.log])
        with self.assertRaises(ValueError):
            native_boot.Mode('native vdm-return noramdisk')              # needs stage

    def test_untyped_session_that_restarts_the_machine_itself(self):
        """multiuser + self_reboot_mode.txt: the capture ends ten seconds after the guest's SF_BENCH_REBOOT line."""
        fakes = Fakes()
        (Path(self.tmp.name) / 'self_reboot_mode.txt').write_text('test\n')
        out = bytearray(b'boot\n')
        alive = threading.Event()
        thread = threading.Thread(target=alive.wait, daemon=True)
        thread.start()
        fakes.console = lambda port: SimpleNamespace(
            done_seen=False, proof=SimpleNamespace(output=out), dev=SimpleNamespace(write=len, flush=lambda: None),
            thread=thread, close=lambda: fakes.log.append(('console_close',)))
        clock = [1000.0]
        real_monotonic, real_sleep = native_stage.time.monotonic, native_stage.time.sleep

        def sleep(seconds):
            clock[0] += seconds
            if clock[0] > 1003 and b'SF_BENCH_REBOOT' not in out:
                out.extend(b'SF_HALT ...\nPD_BENCH_REBOOT\nSF_HALT restarting now\n')
                sleep.announced = clock[0]
        native_stage.time.monotonic, native_stage.time.sleep = (lambda: clock[0]), sleep
        native = native_boot.Mode('native stage vdm-return noiolog noramdisk multiuser')
        try:
            with tempfile.TemporaryDirectory() as directory:
                native_stage.start(native, fakes.backend, fakes.inputs, native_boot.bootargs(
                    native, '-v -s serial=3 rd=uuid boot-uuid=X cpus=8 sfcpuheld=1 '), fakes.raw_bootargs, fakes.layout,
                    Path(directory), 100, fakes.console, lambda d, name, value: fakes.saved.__setitem__(name, value))
        finally:
            native_stage.time.monotonic, native_stage.time.sleep = real_monotonic, real_sleep
            alive.set()
        waited = clock[0] - sleep.announced
        self.assertTrue(10 <= waited < 11, waited)                      # not the runtime bound of 100 s
        self.assertTrue(fakes.saved['capture-core.json']['console_capture_finalized'])

    def test_stage1_is_entered_as_iboot_enters_it(self):
        """The installed stage 1: top_of_kernel_data directly behind the image and the boot arguments page
        (no 64 MiB of room, which the installed machine does not have); stage1room gives the room;
        stage1go takes the command line from the copy of the kernel file and uploads none of it."""
        stage1 = fake_m1n1() + b'chosen.asahi,efi-system-partition=x\nchainload=x;sf/boot.bin\n' + bytes(4)
        (Path(self.tmp.name) / native_stage.STAGE1_TEST).write_bytes(stage1)
        size = (len(stage1) + 0x3fff) & ~0x3fff
        for words, room in (('', 0), (' stage1room', 64 << 20)):
            fakes = Fakes()
            fakes.console = lambda port, f=fakes: SimpleNamespace(
                done_seen=True, proof=SimpleNamespace(output=b'SF_STOPPED\n'), dev=SimpleNamespace(write=len, flush=lambda: None),
                thread=threading.Thread(target=lambda: None), close=lambda: None)
            fakes.run('native stage stage1 noramdisk multiuser vdm-return noiolog' + words, '-v -s serial=3 rd=uuid cpus=8 sfcpuheld=1 ')
            record = fakes.saved['native-start.json']
            self.assertEqual(record['bootargs'], BASE + size + room)
            self.assertEqual(('memalign', 0x10000, size + room + 0x4000), [e for e in fakes.log if e[0] == 'memalign'][0])
            entered = b''.join(fakes.mem[a] for a in range(BASE + size + room, BASE + size + room + 0x4000, 0x4000))
            self.assertEqual(struct.unpack_from('<Q', entered, 32)[0], BASE + size + room + 0x4000)
            self.assertEqual(self.uploaded(fakes, len(stage1)), stage1)
        # stage1go
        args = '-v rd=uuid boot-uuid=u cpus=8 sfcpuheld=1 sfsession_seconds=150 sfmark=1'
        fakes = Fakes(usb=0)
        kernel_file = pdsf_pack.records(pdsf_pack.config_text(args, usb_nodes='remove0'), fakes.inputs.contents['kernel'])
        (Path(self.tmp.name) / native_stage.STAGE1_KERNEL).write_bytes(kernel_file)
        fakes.run('native stage stage1 stage1go noramdisk multiuser vdm-return noiolog', '-v -s serial=3 rd=uuid cpus=8 sfcpuheld=1 ')
        record = fakes.saved['native-start.json']
        self.assertEqual(fakes.saved['before-start.json']['bootargs'], args)
        self.assertEqual(record['image_bytes'], len(stage1))             # nothing but stage 1 went to the machine
        self.assertEqual(record['bootargs'], BASE + size)
        self.assertEqual(sum(e[2] for e in fakes.log if e[0] == 'writemem'), size + 0x4000)
        (Path(self.tmp.name) / native_stage.STAGE1_KERNEL).write_bytes(
            pdsf_pack.records(pdsf_pack.config_text(args, usb_nodes='remove0'), os.urandom(5000)))
        with self.assertRaises(Exception):
            Fakes(usb=0).run('native stage stage1 stage1go noramdisk multiuser vdm-return noiolog', '-v -s serial=3 rd=uuid cpus=8 sfcpuheld=1 ')
        for bad in ('native stage stage1room vdm-return', 'native stage stage1 stage1go noramdisk vdm-return'):
            with self.assertRaises(Exception):
                native_boot.Mode(bad)

    def test_refusals(self):
        with self.assertRaises(ValueError):                             # SMP without the kernel's release path
            Fakes().run('native stage vdm-return', bootargs='-v cpus=8 ')
        fakes = Fakes()
        fakes.backend.ba.top_of_kernel_data += 0x4000                   # not the boot arguments that were collected
        with self.assertRaises(ValueError):
            fakes.run('native stage vdm-return')
        fakes = Fakes()
        fakes.layout['phys_base'] = BASE + (64 << 20)                   # the image would reach into XNU's memory
        with self.assertRaises(ValueError):
            fakes.run('native stage vdm-return')
        fakes = Fakes()
        fakes.backend.iface.readmem = lambda address, size: bytes(size)  # the upload did not arrive
        with self.assertRaises(ValueError):
            fakes.run('native stage vdm-return')
        self.assertNotIn('request', [entry[0] for entry in fakes.log])
        with self.assertRaises(ValueError):
            native_boot.Mode('stage vdm-return')
        with self.assertRaises(ValueError):
            native_boot.Mode('native vdm-return usbkeep')

    def test_real_stage_image_when_built(self):
        built = HERE / 'out' / native_stage.IMAGE
        if not built.is_file():
            self.skipTest('b1/build.sh has not been run here')
        data = built.read_bytes()
        self.assertEqual(pdsf_pack.records_offset(data), len(data))     # the records start where the file ends
        self.assertIn(b'-siliconforest-stage-v1', data)
        self.assertIn(b'PDSF: entering XNU', data)
        image = pdsf_pack.image(data, pdsf_pack.config_text('-v'), b'k' * 64, compress_kernel=False)
        self.assertEqual(pdsf_pack.parse(image, len(data))[0][0][0], pdsf_pack.CONFIG)

    def test_launcher_patch_calls_the_stage_before_anything_is_loaded(self):
        lab = ROOT / 'runs/m1-desktop-010/pcie-m2-host-001/lab'
        if not lab.is_dir() or not shutil.which('patch'):
            self.skipTest('no launcher tree here')
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / 'lab'
            shutil.copytree(lab, copy, ignore=shutil.ignore_patterns('__pycache__'))
            subprocess.run(['patch', '-s', '-d', str(copy), '-p1', '-i', str(HERE.parent / 'launcher-native.patch')],
                           check=True)
            text = (copy / 'integrations/siliconforest/m1-bringup/host_progress.py').read_text()
            compile(text, 'host_progress.py', 'exec')
            worker = text[text.index('def run_worker('):]
            stage = worker.index('native_stage.start(')
            self.assertLess(worker.index('reservation = check_reservation('), stage)
            for later in ('hv = backend.new_hv()', 'hv.init()', 'native_boot.native_init(', 'hv.load_raw(',
                          'load_payloads(hv', 'native_boot.start('):
                self.assertLess(stage, worker.index(later), later)
            self.assertIn("getattr(native, 'stage', False)", worker)   # without the word: the patch as it was


if __name__ == '__main__':
    unittest.main()
