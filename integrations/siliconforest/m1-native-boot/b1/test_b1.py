#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host-only checks of the SiliconForest stage; no hardware, nothing is booted.
    python3 -B integrations/siliconforest/m1-native-boot/b1/test_b1.py -v

src/pdsf.c (container, configuration, Mach-O fileset loader, memory layout, device-tree
rewrite) is compiled for this host and compared with independent Python models of what the
launcher does (m1n1 macho.py prepare_image, hv load_raw/setup_adt, host_progress.load_payloads).
Real inputs are used when they are on this machine and skipped otherwise: the native
collection in the nix store, a private device-tree capture under runs/, the pinned m1n1 source.
"""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import pdsf_pack

KC = Path(os.environ.get('B1_KC', '/nix/store/k2w23az428jr3nmqlca19da00kn4wshn-puredarwin-kc-arm64-0.1/kernel'))
M1N1 = ROOT / 'sources/m1n1-baseline'
PAGE = 0x4000


def align(value, to=PAGE):
    return (value + to - 1) & ~(to - 1)


# ---- Python model of the Apple device tree ----
class Node:
    def __init__(self):
        self.props, self.children = [], []     # props: [name, raw size word, value bytes]

    def get(self, name):
        for prop in self.props:
            if prop[0] == name:
                return prop[2]
        return None

    @property
    def name(self):
        return self.get('name').split(b'\0')[0].decode()

    def set(self, name, value, only_existing=False, keep_flag=False):
        for prop in self.props:
            if prop[0] == name:
                prop[1], prop[2] = (prop[1] & 0x80000000 if keep_flag else 0) | len(value), value
                return True
        if not only_existing:
            self.props.append([name, len(value), value])
        return not only_existing

    def find(self, path):
        node = self
        for part in [p for p in path.split('/') if p]:
            node = next((c for c in node.children if c.name == part), None)
            if node is None:
                return None
        return node


def adt_parse(data, offset=0):
    node = Node()
    nprops, nchildren = struct.unpack_from('<II', data, offset)
    offset += 8
    for _ in range(nprops):
        name = data[offset:offset + 32].split(b'\0')[0].decode()
        raw, = struct.unpack_from('<I', data, offset + 32)
        size = raw & 0x7fffffff
        node.props.append([name, raw, data[offset + 36:offset + 36 + size]])
        offset += 36 + align(size, 4)
    for _ in range(nchildren):
        child, offset = adt_parse(data, offset)
        node.children.append(child)
    return node, offset


def adt_build(node):
    out = [struct.pack('<II', len(node.props), len(node.children))]
    for name, raw, value in node.props:
        out.append(name.encode().ljust(32, b'\0') + struct.pack('<I', (raw & 0x80000000) | len(value)))
        out.append(value + bytes(-len(value) % 4))
    return b''.join(out + [adt_build(child) for child in node.children])


def model_edit(tree, sets=(), remove=(), prune=False, oslog=False, dfp=None):
    """The launcher's edits (hv setup_adt, load_raw, host_progress), on the model."""
    removed = 0
    for path in remove:
        parent = tree.find(path.rsplit('/', 1)[0])
        victim = tree.find(path)
        if parent is not None and victim is not None:
            parent.children.remove(victim)
            removed += 1
    if prune:
        cpus = tree.find('/cpus')
        keep = [c for c in cpus.children if (c.get('state') or b'')[:8] == b'running\0']
        removed += len(cpus.children) - len(keep)
        cpus.children = keep
    for node_path, prop, value, only in sets:
        node = tree.find(node_path)
        if node is not None:
            node.set(prop, value, only)
    if oslog:
        armio = tree.find('/arm-io')
        for node in armio.children + [n for c in armio.children for n in c.children]:
            names = node.get('segment-names')
            if names is None:
                continue
            parts = names.rstrip(b'\0').split(b';')
            if b'__OS_LOG' not in parts:
                continue
            idx = parts.index(b'__OS_LOG')
            ranges = node.get('segment-ranges')
            node.set('segment-names', b';'.join(parts[:idx] + parts[idx + 1:]) + b'\0', keep_flag=True)
            node.set('segment-ranges', ranges[:idx * 32] + ranges[idx * 32 + 32:], keep_flag=True)
    if dfp:
        for node in tree.find('/arm-io').children:
            ends = node.get('dfp-endpoints')
            if node.name.startswith('display-crossbar') and ends is not None:
                parts = ends.split(b'\0')
                node.set('dfp-endpoints', b'\0'.join(p for p in parts if p != dfp.encode()), keep_flag=True)
    return removed


def prop(name, value):
    return [name, len(value), value]


def synthetic_adt():
    def node(name, props=(), children=()):
        n = Node()
        n.props = [prop('name', name.encode() + b'\0')] + [list(p) for p in props]
        n.children = list(children)
        return n
    rng = struct.pack('<QQQII', 0x8_1000_0000, 0xf_0000_0000, 0, 0x4000, 0)
    seg = node('dcp', [prop('segment-names', b'__TEXT;__OS_LOG;__DATA\0'),
                       prop('segment-ranges', rng + rng.replace(b'\x10', b'\x20', 1) + rng.replace(b'\x10', b'\x30', 1))],
               [node('iop-dcp-nub', [prop('segment-names', b'__OS_LOG\0'), prop('segment-ranges', rng)])])
    cpus = node('cpus', [], [
        node('cpu0', [prop('state', b'running\0'), prop('cpu-id', struct.pack('<I', 0))]),
        node('cpu1', [prop('state', b'waiting\0'), prop('cpu-id', struct.pack('<I', 1))]),
        node('cpu2', [prop('state', b'waiting\0'), prop('cpu-id', struct.pack('<I', 2))])])
    mm = node('memory-map', [prop('SEPFW', struct.pack('<QQ', 0x801000000, 0x7c0000)),
                             prop('TrustCache', struct.pack('<QQ', 0x802000000, 0x4321)),
                             prop('DeviceTree', struct.pack('<QQ', 0x803000000, 0x60000)),
                             prop('BootArgs', struct.pack('<QQ', 0x803100000, 0x4000))])
    chosen = node('chosen', [prop('chip-id', struct.pack('<I', 0x8103)), prop('odd', b'abc')], [mm])
    hpm = node('i2c0', [], [node('hpmBusManager', [], [node('hpm0'), node('hpm1')])])
    armio = node('arm-io', [], [
        node('uart0', [prop('AAPL,phandle', struct.pack('<I', 77))]), seg, hpm,
        node('dart-usb0'), node('dart-usb1'), node('atc-phy0', [], [node('child')]),
        node('display-crossbar0', [prop('dfp-endpoints', b'atc0\0atc1\0dp\0')])])
    root = node('device-tree', [prop('model', b'MacBookPro17,1\0'), ['flagged', 0x80000004, b'\1\2\3\4']],
                [chosen, cpus, node('defaults', [prop('serial-device', struct.pack('<I', 5))]), armio])
    return adt_build(root)


# ---- Python model of the Mach-O loader (m1n1 macho.py load_info / prepare_image) ----
def macho_model(raw):
    ncmds, = struct.unpack_from('<I', raw, 16)
    offset, segments, entry = 32, [], None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', raw, offset)
        if cmd == 0x19:
            segments.append(struct.unpack_from('<4Q', raw, offset + 24))
        elif cmd == 5:
            entry, = struct.unpack_from('<Q', raw, offset + 16 + 32 * 8)
        offset += size
    vmin = min(s[0] for s in segments)
    vmax = max(s[0] + s[1] for s in segments)
    image = bytearray(vmax - vmin)
    for vmaddr, _, fileoff, filesize in segments:
        image[vmaddr - vmin:vmaddr - vmin + filesize] = raw[fileoff:fileoff + filesize]
    return vmin, vmax, entry, len(segments), bytes(image)


def tiny_fileset():
    def segment(name, vmaddr, vmsize, fileoff, filesize):
        return struct.pack('<II16s4Q4I', 0x19, 72, name, vmaddr, vmsize, fileoff, filesize, 0, 0, 0, 0)
    base = 0xfffffe0007004000
    thread = struct.pack('<IIII', 5, 16 + 34 * 8, 6, 68) + struct.pack('<34Q', *([0] * 32 + [base + 0x4010, 0]))
    cmds = (segment(b'__TEXT', base, 0x8000, 0x4000, 0x8000) + segment(b'__DATA', base + 0xc000, 0x8000, 0xc000, 0x1000) +
            segment(b'__EMPTY', base + 0x14000, 0, 0, 0) + thread)
    header = struct.pack('<8I', 0xfeedfacf, 0x0100000c, 2, 0xc, 4, len(cmds), 0, 0)
    body = (header + cmds).ljust(0x4000, b'\0')
    return body + bytes(range(256)) * (0x9000 // 256)


class Stage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        cls.exe = cls.dir / 'pdsf_host'
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-DPDSF_HOST', '-I', str(HERE / 'src'),
                        str(HERE / 'src/pdsf.c'), str(HERE / 'test/pdsf_host.c'), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_c(self, *args):
        result = subprocess.run([str(self.exe)] + [str(a) for a in args], capture_output=True, text=True,
                                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))   # the driver exits without freeing
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.splitlines()

    def write(self, name, data):
        path = self.dir / name
        path.write_bytes(data)
        return path

    # -- container --
    def test_container_roundtrip(self):
        kernel, ramdisk = os.urandom(70001), bytes(300000) + os.urandom(5000)
        config = pdsf_pack.config_text('-v rd=md0 cpus=8 sfcpuheld=1', usb_nodes='remove0', wdt=120)
        m1n1 = bytearray(0x8000)
        m1n1[0x808:0x810] = b'AURALOAD'
        struct.pack_into('<Q', m1n1, 0x818, 0x8000)
        data = pdsf_pack.image(bytes(m1n1), config, kernel, ramdisk)
        self.assertEqual(pdsf_pack.records_offset(data), 0x8000)
        found, end = pdsf_pack.parse(data, 0x8000)
        self.assertEqual([f[0] for f in found], [1, 2, 3])
        self.assertTrue(all(f[4] for f in found))
        lines = self.run_c('scan', self.write('image.bin', data), 0x8000)
        self.assertEqual(lines[0], 'ret 0')
        for line, (kind, flags, stored, raw, _, offset) in zip(lines[1:], found):
            got = [int(x) for x in line.split()]
            self.assertEqual(got[:4] + got[5:], [kind, flags, stored, raw, offset])
            self.assertEqual(got[4], zlib.crc32(data[offset:offset + stored]))
            if flags & pdsf_pack.F_GZIP:
                self.assertEqual(len(zlib.decompress(data[offset:offset + stored], 31)), raw)
        self.assertEqual(lines[-1], 'end %d' % end)
        self.assertEqual(zlib.decompress(data[found[2][5]:found[2][5] + found[2][2]], 31), ramdisk)

    def test_container_refusals(self):
        good = pdsf_pack.records(b'bootargs=x\n', b'k' * 100, compress_kernel=False)
        self.assertEqual(self.run_c('scan', self.write('good', good), 0)[0], 'ret 0')
        self.assertEqual(self.run_c('scan', self.write('none', bytes(64)), 0)[0], 'ret 1')
        self.assertEqual(self.run_c('scan', self.write('linux', b'\x1f\x8b' + bytes(62)), 0)[0], 'ret 1')
        cases = {
            'cut': good[:-40],                                           # no END record
            'version': good[:4] + b'\x02\x00' + good[6:],
            'type': good[:6] + b'\x09\x00' + good[8:],
            'flags': good[:8] + b'\x02\x00\x00\x00' + good[12:],
            'long': good[:16] + struct.pack('<Q', 1 << 40) + good[24:],
            'twice': good[:-32] + good,                                  # two config records
            'plain-sizes': good[:24] + struct.pack('<Q', 99) + good[32:],
        }
        for name, data in cases.items():
            ret = int(self.run_c('scan', self.write(name, data), 0)[0].split()[1])
            self.assertLess(ret, 0, name)

    # -- configuration --
    def test_pack_bootargs_firmware_capacity(self):
        # Default M1 revision 2 must reject the 621-byte package that reached
        # SF_STOPPED before XNU. Other revisions require an explicit choice.
        for revision, capacity in ((1, 256), (2, 608), (3, 1024)):
            text = pdsf_pack.config_text('a' * (capacity - 1), bootargs_revision=revision)
            self.assertEqual(text, b'bootargs=' + b'a' * (capacity - 1) + b'\n')
            with self.assertRaises(ValueError):
                pdsf_pack.config_text('a' * capacity, bootargs_revision=revision)
        for bad in ('a' * 608, 'a' * 621, '', 'a\tb', '\u00e9'):
            with self.assertRaises(ValueError):
                pdsf_pack.config_text(bad)
        with self.assertRaises(ValueError):
            pdsf_pack.config_text('x', bootargs_revision=4)
        # A rejected CLI invocation must neither create nor truncate its output.
        kernel = self.write('pack-kernel', b'kernel')
        output = self.write('pack-output', b'rollback')
        cmd = [sys.executable, str(HERE / 'pdsf_pack.py'), 'pack', '--kernel', str(kernel),
               '--bootargs', 'a' * 621, '-o', str(output)]
        result = subprocess.run(cmd, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'revision 2', result.stderr)
        self.assertEqual(output.read_bytes(), b'rollback')
        output.unlink()
        result = subprocess.run(cmd, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    def test_config(self):
        seed = os.urandom(256)
        text = pdsf_pack.config_text('-v serial=0x13 cpus=8 sfcpuheld=1 rd=md0', display=1, usb_nodes='remove1',
                                     sprr=0, amx=1, wdt=90, reserve_mb=128, seed_hex=seed.hex(), serial=0,
                                     xnu_base='0x852ebc000')
        lines = self.run_c('config', self.write('config', b'# a comment\n\n' + text))
        self.assertEqual(lines, ['ret 0 ', 'bootargs -v serial=0x13 cpus=8 sfcpuheld=1 rd=md0', 'display 1', 'usb 1',
                                 'sprr 0', 'amx 1', 'wdt 90', 'reserve 128', 'serial 0',
                                 'seed 1 %02x%02x' % (seed[0], seed[255]), 'cpus 8', 'held 1', 'base 852ebc000'])
        lines = self.run_c('config', self.write('config', b'bootargs=-s cpus=1 xpdcpuheld=1\n'))
        self.assertEqual(lines[1:], ['bootargs -s cpus=1 xpdcpuheld=1', 'display 1', 'usb -1', 'sprr 1', 'amx 1',
                                     'wdt 0', 'reserve 64', 'serial 1', 'seed 0 0000', 'cpus 1', 'held 0', 'base 0'])
        self.assertEqual(self.run_c('config', self.write('config', b'bootargs=-v\n'))[-3], 'cpus 0')
        for bad in (b'display=1\n', b'bootargs=x\nwdt=20\n', b'bootargs=x\nwdt=171\n', b'bootargs=x\nnope=1\n',
                    b'bootargs=x\nusb-nodes=remove2\n', b'bootargs=x\nseed-hex=abcd\n', b'bootargs=x\nline\n',
                    b'bootargs=' + b'a' * 1024 + b'\n', b'bootargs=a\tb\n', b'bootargs=x\nreserve-mb=8\n',
                    b'bootargs=x\nxnu-base=0x852ebc001\n', b'bootargs=x\nxnu-base=852ebc000\n'):
            self.assertTrue(self.run_c('config', self.write('config', bad))[0].startswith('ret -1'), bad[:40])

    # -- Mach-O --
    def check_macho(self, raw):
        vmin, vmax, entry, count, image = macho_model(raw)
        out = self.dir / 'image.out'
        lines = self.run_c('macho', self.write('kc', raw), out)
        self.assertEqual(lines[0], 'ret 0 ')
        self.assertEqual([int(x) for x in lines[1].split()], [vmin, vmax, entry, count])
        self.assertEqual(hashlib.sha256(out.read_bytes()).digest(), hashlib.sha256(image).digest())
        return vmin, vmax, entry

    def test_macho_synthetic(self):
        raw = tiny_fileset()
        self.check_macho(raw)
        for name, bad in (('magic', b'\0' + raw[1:]), ('type', raw[:12] + struct.pack('<I', 2) + raw[16:]),
                          ('cut', raw[:0x9000]), ('cmds', raw[:20] + struct.pack('<I', 1 << 30) + raw[24:])):
            self.assertTrue(self.run_c('macho', self.write('bad', bad))[0].startswith('ret -1'), name)

    @unittest.skipUnless(KC.is_file(), 'native collection not in the nix store')
    def test_macho_native_collection(self):
        vmin, vmax, entry = self.check_macho(KC.read_bytes())
        self.assertEqual(vmin % PAGE, 0)
        self.assertTrue(vmin <= entry < vmax)

    # -- layout --
    def layout_model(self, phys, ram_end, image, devtree, capacity, sepfw, preoslog, tc, ramdisk):
        # m1n1 hv load_raw
        guest = phys + (16 << 20) + align(devtree) + align(tc)
        size = align(image)
        sepfw_off = size
        size += align(sepfw)
        preoslog_off = size
        size += preoslog
        bootargs_off = size
        # host_progress.load_payloads
        adt = align(guest + bootargs_off + PAGE)
        tc_at = adt + capacity
        ramdisk_at = tc_at + align(tc)
        return dict(phys_base=phys, virt_base=0xfffffe0010000000 + (phys & ((32 << 20) - 1)), mem_size=ram_end - phys,
                    guest_base=guest, sepfw=guest + sepfw_off, preoslog=guest + preoslog_off, bootargs=guest + bootargs_off,
                    adt=adt, tc=tc_at, ramdisk=ramdisk_at, top=ramdisk_at + align(ramdisk))

    def layout_c(self, *args):
        lines = self.run_c('layout', *args)
        if lines[0] != 'ret 0':
            return int(lines[0].split()[1])
        return dict(zip(('phys_base', 'virt_base', 'mem_size', 'guest_base', 'sepfw', 'preoslog', 'bootargs', 'adt',
                         'tc', 'ramdisk', 'top'), (int(x) for x in lines[1:])))

    def test_layout_is_the_launchers(self):
        stage_top, reserve, ram_end = 0x8_0a12_3440, 64 << 20, 0xb_dc00_0000
        sizes = (0x3160000, 0x5c284, align(0x5c284 + 1024), 0x5d8000, 0x40000, 0x58000, 384 << 20)
        # the stage chooses: the next 32 MiB boundary above its own memory and reserve
        phys = align(stage_top + reserve, 32 << 20)
        got = self.layout_c(stage_top, reserve, 0, ram_end, *sizes)
        self.assertEqual(got, self.layout_model(phys, ram_end, *sizes))
        self.assertGreaterEqual(got['phys_base'], stage_top + reserve)
        self.assertEqual(self.layout_c(stage_top, reserve, 0, phys + (32 << 20), *sizes), -2)
        # the launcher names its own phys_base (boot nb08's): every address is then the launcher's
        fixed = 0x8_52eb_c000
        got = self.layout_c(stage_top, reserve, fixed, ram_end, *sizes)
        self.assertEqual(got, self.layout_model(fixed, ram_end, *sizes))
        self.assertEqual(got['virt_base'], 0xfffffe0010000000 + 0xebc000)
        self.assertEqual(sizes[2], align(0x5c284))            # and the tree's capacity is the launcher's
        self.assertEqual(self.layout_c(stage_top, reserve, stage_top + reserve - PAGE & ~(PAGE - 1), ram_end, *sizes), -3)
        self.assertEqual(self.layout_c(stage_top, reserve, fixed + 4, ram_end, *sizes), -3)

    # -- device tree --
    def rebuild(self, data, capacity=None, sets=(), remove=(), prune=False, oslog=False, dfp=None):
        args = ['adt', self.write('adt.in', data), self.dir / 'adt.out', capacity or len(data) + 0x4000]
        args += ['prune'] * prune + ['oslog'] * oslog + (['dfp', dfp] if dfp else [])
        for path in remove:
            args += ['remove', path]
        for node, name, value, only in sets:
            args += ['setx' if only else 'set', node, name, value.hex()]
        lines = self.run_c(*args)
        status = dict(zip(lines[0].split()[::2], (int(x) for x in lines[0].split()[1::2])))
        return status, ((self.dir / 'adt.out').read_bytes() if status['ret'] > 0 else None), lines[1:]

    def check_edit(self, data, **edit):
        tree, end = adt_parse(data)
        self.assertEqual(adt_build(tree), data[:end])
        removed = model_edit(tree, **{k: v for k, v in edit.items()})
        want = adt_build(tree)
        status, got, used = self.rebuild(data, **edit)
        self.assertEqual(status['size'], end)
        self.assertEqual(status['ret'], len(want))
        self.assertEqual(got, want)
        self.assertEqual(status['removed'], removed)
        again, end2 = adt_parse(got)
        self.assertEqual(end2, len(got))
        return status, got, used, again

    USB0 = ['/arm-io/dart-usb0', '/arm-io/atc-phy0', '/arm-io/usb-drd0', '/arm-io/i2c0/hpmBusManager/hpm0',
            '/arm-io/not-there']

    def edits(self, tree_has_preoslog=False):
        seed = os.urandom(256)
        return dict(sets=[
            ('/chosen', 'random-seed', seed, False),
            ('/chosen', 'kernel-ctrr-to-be-enabled', struct.pack('<I', 0), False),
            ('/chosen/memory-map', 'SEPFW', struct.pack('<QQ', 0x8_4000_0000, 0x7c0000), True),
            ('/chosen/memory-map', 'TrustCache', struct.pack('<QQ', 0x8_4100_0000, 0x4321), True),
            ('/chosen/memory-map', 'DeviceTree', struct.pack('<QQ', 0x8_4200_0000, 0x64000), False),
            ('/chosen/memory-map', 'BootArgs', struct.pack('<QQ', 0x8_4300_0000, 0x4000), False),
            ('/chosen/memory-map', 'preoslog', struct.pack('<QQ', 0x8_4400_0000, 0x8000), True),
            ('/chosen/memory-map', 'Kernel-mach_header', struct.pack('<QQ', 0x8_4500_0000, 0), True),
            ('/chosen/memory-map', 'RAMDisk', struct.pack('<QQ', 0x8_5000_0000, 384 << 20), False),
            ('/defaults', 'serial-device', struct.pack('<I', 77), False)])

    def test_adt_identity(self):
        data = synthetic_adt()
        status, got, _ = self.rebuild(data)
        self.assertEqual(got, data)

    def test_adt_synthetic_edits(self):
        data = synthetic_adt()
        status, got, used, tree = self.check_edit(data, remove=self.USB0, prune=True, oslog=True, dfp='atc0',
                                                  **self.edits())
        self.assertEqual((status['removed'], status['oslog'], status['dfp']), (5, 2, 1))
        self.assertEqual([int(u.split()[1]) for u in used], [1, 1, 1, 1, 1, 1, 0, 0, 1, 1])
        self.assertEqual([c.name for c in tree.find('/cpus').children], ['cpu0'])
        self.assertIsNone(tree.find('/arm-io/dart-usb0'))
        self.assertIsNotNone(tree.find('/arm-io/dart-usb1'))
        self.assertIsNone(tree.find('/arm-io/i2c0/hpmBusManager/hpm0'))
        self.assertEqual(tree.find('/arm-io/dcp').get('segment-names'), b'__TEXT;__DATA\0')
        self.assertEqual(len(tree.find('/arm-io/dcp').get('segment-ranges')), 64)
        self.assertEqual(tree.find('/arm-io/dcp/iop-dcp-nub').get('segment-names'), b'\0')
        self.assertEqual(tree.find('/arm-io/display-crossbar0').get('dfp-endpoints'), b'atc1\0dp\0')
        self.assertEqual(tree.find('/chosen/memory-map').get('RAMDisk'), struct.pack('<QQ', 0x8_5000_0000, 384 << 20))
        self.assertIsNone(tree.find('/chosen/memory-map').get('preoslog'))
        self.assertEqual(tree.get('flagged'), b'\1\2\3\4')
        self.assertEqual([p[1] for p in tree.props if p[0] == 'flagged'], [0x80000004])
        # SMP: nothing pruned
        _, _, _, tree = self.check_edit(data, oslog=True, **self.edits())
        self.assertEqual(len(tree.find('/cpus').children), 3)

    def test_adt_capacity_and_damage(self):
        data = synthetic_adt()
        status, got, _ = self.rebuild(data, capacity=len(data) + 100, **self.edits())
        self.assertLess(status['ret'], 0)        # the additions do not fit: refused, nothing past the buffer
        status, got, _ = self.rebuild(data, capacity=len(data))
        self.assertEqual(got, data)
        for cut in (7, 40, len(data) // 2, len(data) - 1):
            status, got, _ = self.rebuild(data[:cut], capacity=len(data))
            self.assertLess(status['ret'], 0, cut)
        huge = data[:4] + struct.pack('<I', 1 << 30) + data[8:]
        self.assertLess(self.rebuild(huge)[0]['ret'], 0)

    def private_adts(self):
        return sorted((ROOT / 'runs').glob('m1-*/attempt-*/guest/original-adt.bin'))[:3]

    def test_adt_real_capture(self):
        captures = self.private_adts()
        if not captures:
            self.skipTest('no private device-tree capture on this machine')
        for path in captures:
            data = path.read_bytes()
            tree, end = adt_parse(data)
            self.assertEqual(tree.find('/chosen').get('chip-id'), struct.pack('<I', 0x8103))
            usb0 = ['/arm-io/dart-usb0', '/arm-io/atc-phy0', '/arm-io/usb-drd0', '/arm-io/acio0', '/arm-io/acio-cpu0',
                    '/arm-io/dart-acio0', '/arm-io/apciec0', '/arm-io/dart-apciec0', '/arm-io/apciec0-piodma',
                    '/arm-io/i2c0/hpmBusManager/hpm0', '/arm-io/i2c0/hpmBusManager1/hpm0',
                    '/arm-io/i2c3/hpmBusManager0/hpm0', '/arm-io/nub-spmi-a0/hpm0', '/arm-io/atc0-dpxbar',
                    '/arm-io/atc0-dpphy', '/arm-io/atc0-dpin0', '/arm-io/atc0-dpin1', '/arm-io/atc-phy0']
            for smp in (False, True):
                status, got, used, after = self.check_edit(data[:end], remove=usb0, prune=not smp, oslog=True,
                                                           dfp='atc0', **self.edits())
                cpus = after.find('/cpus').children
                self.assertEqual(len(cpus), 8 if smp else 1)
                self.assertEqual(sum((c.get('state') or b'')[:8] == b'running\0' for c in cpus), 1)
                self.assertEqual(len(after.find('/chosen').get('random-seed')), 256)
                self.assertLessEqual(len(got), align(end + 1024))    # fits the capacity the stage reserves
                for node in after.find('/arm-io').children:
                    self.assertNotIn(b'__OS_LOG', node.get('segment-names') or b'')
            # usb-nodes=keep: every node of the tree survives
            status, got, used, after = self.check_edit(data[:end], oslog=True, **self.edits())
            self.assertEqual(status['removed'], 0)
            self.assertIsNotNone(after.find('/arm-io/usb-drd0'))

    # -- the m1n1 side: what can be checked without a target --
    def test_el2_page_is_the_booted_one(self):
        import native_stub
        text = (HERE / 'build.sh').read_text()
        self.assertIn('el2_drop.S', text)
        self.assertIn('STUB_SHA256', text)
        blob = bytes.fromhex(native_stub.STUB_HEX)
        self.assertEqual(hashlib.sha256(blob).hexdigest(), native_stub.STUB_SHA256)
        source = (HERE / 'src/pdsf_boot.c').read_text()
        for name, value in (('EL2_PARAMS', 0x3f00), ('EL2_HOLD_ACKS', 0x3f40), ('EL2_BOOT_ENTRY', 4),
                            ('EL2_HOLD_ENTRY', 8)):
            self.assertRegex(source, r'#define %s\s+%s\b' % (name, hex(value) if value > 9 else value))
        self.assertEqual(blob[0x3f00:0x3f05], b'PDNB1')

    def test_stage_never_touches_storage(self):
        source = (HERE / 'src/pdsf_boot.c').read_text() + (HERE / 'src/pdsf.c').read_text()
        for word in ('nvme_', 'boot_storage', 'rust_load_image', 'chainload_load', 'nvram', 'usb_init'):
            self.assertNotIn(word, source.replace('no NVMe, no NVRAM', ''), word)

    @unittest.skipUnless((M1N1 / '.git').exists(), 'pinned m1n1 source not on this machine')
    def test_patch_applies_to_the_pin(self):
        head = subprocess.run(['git', '-C', str(M1N1), 'rev-parse', 'HEAD'], capture_output=True, text=True, check=True)
        self.assertTrue(head.stdout.startswith('121fcbf'))
        for patch in sorted((HERE / 'patches').glob('*.patch')):
            subprocess.run(['git', '-C', str(M1N1), 'apply', '--check', str(patch)], check=True)
        touched = subprocess.run(['git', '-C', str(M1N1), 'status', '--porcelain', '--untracked-files=no'],
                                 capture_output=True, text=True, check=True)
        self.assertEqual(touched.stdout, '')      # --check changed nothing in the pinned tree


class RootRule(Stage):
    """src/pdsf.c pdsf_root_check: which root a command line may name."""
    U = '0A1B2C3D-1111-2222-3333-444455556666'

    def root(self, have_ramdisk, built_for, cmdline):
        return self.run_c('root', 1 if have_ramdisk else 0, built_for, cmdline)[0].strip()

    def test_ram_root_needs_the_record(self):
        self.assertEqual(self.root(True, '', '-v rd=md0 cpus=8'), 'rc=0 kind=1')
        self.assertEqual(self.root(True, self.U, 'rd=md0'), 'rc=0 kind=1')
        self.assertTrue(self.root(False, '', '-v rd=md0 cpus=8').startswith('rc=-1 kind=0 rd=md0 without a RAM root record'))
        self.assertTrue(self.root(True, self.U, 'rd=md0 boot-uuid=' + self.U).startswith('rc=-1'))

    def test_disk_root_is_exactly_the_one_built_for(self):
        ok = '-v rd=uuid boot-uuid=%s sfnvme=3 cpus=8' % self.U
        self.assertEqual(self.root(False, self.U, ok), 'rc=0 kind=2')
        self.assertEqual(self.root(True, self.U, ok), 'rc=0 kind=2')          # a RAM disk beside it changes nothing
        other = self.U[:-1] + '7'
        for built, cmdline, why in (
                ('', ok, 'built for no disk root'),
                (other, ok, 'not the filesystem this stage was built for'),
                (self.U, ok.replace(self.U, self.U.lower()), 'not the filesystem'),          # compared as text
                (self.U, ok.replace(self.U, self.U + '0'), 'not the filesystem'),
                (self.U, ok.replace(self.U, self.U[:-1]), 'not the filesystem'),
                (self.U, '-v rd=uuid cpus=8', 'without boot-uuid='),
                (self.U, ok + ' boot-uuid=' + other, 'more than one boot-uuid='),
                (self.U, 'boot-uuid=' + other + ' ' + ok, 'more than one boot-uuid='),
                (self.U, ok + ' rd=md0', 'more than one rd='),
                (self.U, 'rd=md0 ' + ok, 'more than one rd='),
                (self.U, '-v boot-uuid=%s cpus=8' % self.U, 'no rd= argument'),
                (self.U, '-v rd=disk0s3 boot-uuid=%s' % self.U, 'neither md0 nor uuid'),
                (self.U, '-v rd=uuidx boot-uuid=%s' % self.U, 'neither md0 nor uuid'),
                (self.U, '-v xrd=uuid xboot-uuid=%s' % self.U, 'no rd= argument'),
                (self.U, '', 'no rd= argument')):
            out = self.root(False, built, cmdline)
            self.assertTrue(out.startswith('rc=-1 kind=0 ') and why in out, (built, cmdline, out))

    def test_the_stage_uses_the_rule_twice_and_nothing_else(self):
        source = (HERE / 'src/pdsf_boot.c').read_text()
        self.assertEqual(source.count('pdsf_root_check('), 2)       # the configuration, and the page as XNU reads it
        self.assertNotIn('pdsf_cmdline_has(cfg.bootargs, "rd=md0")', source)
        self.assertIn('#include "pdsf_root.h"', source)


if __name__ == '__main__':
    unittest.main()
