#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host test of the install kit (run build.sh first): the installer's OWN code (the copy in
out/mac-kit/installer, Asahi installer e93743d) is run on this workstation against our
installer data and package, with macOS's tools replaced by recorders. It shows what the
installer will ask diskutil and newfs_msdos for, what it copies onto the boot partition and
what it writes as the stub's boot object. Nothing is partitioned, mounted or formatted here.

    python3 -I -B test_kit.py [out directory]
"""
import hashlib, json, os, pathlib, struct, subprocess, sys, tempfile, types, unittest

HERE = pathlib.Path(__file__).resolve().parent
OUT = pathlib.Path(sys.argv.pop(1)) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else HERE / 'out'
KIT = OUT / 'mac-kit' / 'installer'
sys.path.insert(0, str(KIT))
sys.path.insert(0, str(HERE.parent / 'b1'))
os.chdir(KIT)
os.environ.update(DISTRO='SiliconForest', REPO_BASE=str(KIT))
import util, m1n1, osinstall, diskutil      # the installer's modules, as shipped in the kit
import pdsf_pack

FREE_BLOCKS, STUB = 741417, util.align_down(util.psize('2.5GB'), util.psize('1MiB'))
PARTUUID = '0FC63DAF-0000-4000-8000-000053464254'
# the stage 1 in the stub of the installed machine (build.sh STAGE1_INSTALLED)
STAGE1_INSTALLED = '44c1ef0bda96224f5fe8c6228cc12ec1966b7781f38844cea960b21b47654318'


class Recorder:
    """Stands in for the installer's DiskUtil: records the calls, hands back made-up partitions."""
    def __init__(self, tmp):
        self.calls, self.tmp = [], tmp

    def addPartition(self, after, fs, label, size):
        self.calls.append(('addPartition', after, fs, label, size))
        return diskutil.Partition(name='disk0s99', offset=0, size=size, free=False, type='EFI', uuid=PARTUUID)

    def mount(self, name):
        self.calls.append(('mount', name))
        path = os.path.join(self.tmp, 'mnt-' + name); os.makedirs(path, exist_ok=True)
        return path


class Kit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.load(open('installer_data.json'))
        cls.template = cls.data['os_list'][0]
        cls.tmp = tempfile.mkdtemp()
        cls.dutil = Recorder(cls.tmp)
        cls.ran = []
        cls.real_run = subprocess.run
        subprocess.run = lambda args, **kw: cls.ran.append(args) or types.SimpleNamespace(stdout=b'', returncode=0)
        cls.osins = osinstall.OSInstaller(cls.dutil, cls.data, cls.template)
        cls.osins.load_package()
        cls.stub = types.SimpleNamespace(icon_path=os.path.join(cls.tmp, 'VolumeIcon.icns'),
                                         boot_obj_path=os.path.join(cls.tmp, 'boot.bin'))
        cls.osins.partition_disk('disk0s98', None)
        cls.osins.install(cls.stub)
        subprocess.run = cls.real_run

    def test_one_entry_named_siliconforest(self):
        self.assertEqual(len(self.data['os_list']), 1)
        self.assertEqual(self.osins.name, 'SiliconForest')
        self.assertFalse(self.osins.expandable)            # no question about sizes
        self.assertFalse(self.osins.needs_firmware)        # no firmware collection for Linux
        self.assertEqual(self.osins.idata_targets, [])     # nothing of the installer's data on our partition
        self.assertNotIn('extras', self.template)

    def test_fits_the_free_space(self):
        free = FREE_BLOCKS * 4096
        need = STUB + self.osins.min_recommended_size
        self.assertEqual(STUB, 2499805184)
        self.assertEqual(self.osins.min_size, 500 << 20)
        # main.py action_install_into_free: offered when align_down(free, 1 MiB) >= STUB + min_recommended_size
        self.assertGreaterEqual(util.align_down(free, 1 << 20), need)
        # and with room for an alignment of either partition's start to 1 MiB
        self.assertGreaterEqual(free - need, 2 << 20)

    def test_what_diskutil_is_asked(self):
        # one partition after the stub: type EFI, not formatted by diskutil, 500 MiB
        self.assertEqual([c for c in self.dutil.calls if c[0] == 'addPartition'],
                         [('addPartition', 'disk0s98', '%EFI%', '%noformat%', 500 << 20)])
        self.assertEqual(self.ran, [['newfs_msdos', '-F', '32', '-v', 'SFBOOT - Si', '/dev/rdisk0s99']])

    def test_boot_partition_content(self):
        mnt = pathlib.Path(self.tmp) / 'mnt-disk0s99'
        files = sorted(str(p.relative_to(mnt)) for p in mnt.rglob('*') if p.is_file())
        self.assertEqual(files, ['sf/boot.bin', 'sf/kernel.bin'])
        # sf/boot.bin: stage 2 alone, nothing behind the image
        boot = (mnt / 'sf/boot.bin').read_bytes()
        self.assertEqual(boot, (OUT / 'boot.bin').read_bytes())
        self.assertEqual(boot, (OUT / 'sf-stage2.bin').read_bytes())
        self.assertEqual(struct.unpack_from('<Q', boot, 0x818)[0], len(boot))
        # sf/kernel.bin: the record list alone
        data = (mnt / 'sf/kernel.bin').read_bytes()
        self.assertEqual(data, (OUT / 'kernel.bin').read_bytes())
        self.assertEqual(pdsf_pack.records_offset(data), 0)
        found, end = pdsf_pack.parse(data, 0)
        self.assertEqual([i[0] for i in found], [pdsf_pack.CONFIG, pdsf_pack.KERNEL])
        self.assertTrue(all(i[4] for i in found))
        self.assertEqual(end, len(data))

    def test_stage1_can_start_boot_bin(self):
        # m1n1 src/chainload.c chainload_image: with stage 1's heap in use, an image whose end
        # (ALIGN_UP(file + variable lines + 4, 16 KiB) from stage 1's base) lies above the
        # top_of_kernel_data stage 1 inherited is refused. Certain room: stage 1's own image.
        stage1, boot = (OUT / 'sf-stage1.bin').read_bytes(), (OUT / 'boot.bin').read_bytes()
        self.assertEqual(hashlib.sha256(stage1).hexdigest(), STAGE1_INSTALLED)
        self.assertEqual(len(stage1), 4325376)
        lines = ('chosen.asahi,m1n1-stage1-version=%s\nchosen.asahi,efi-system-partition=%s\n'
                 % (m1n1.get_version(str(OUT / 'sf-stage1.bin')).split('##')[-1], PARTUUID.lower())).encode()
        self.assertLess(len(lines), 512)
        span = lambda size, extra: (size + extra + 4 + 16383) // 16384 * 16384
        self.assertLessEqual(span(len(boot), 512) + (32 << 10), len(stage1))
        # the file of the first install (stage 2 + records, 14,423,700 bytes) does not fit: the refusal the
        # installed machine showed
        self.assertGreater(span(14423700, len(lines)), len(stage1))
        # stage 2 reads the kernel file itself and cannot be told to chainload; stage 1 knows no kernel file
        self.assertIn(b'sf/kernel.bin', boot)
        self.assertIn(b'chainload=, boot= and other payloads are not accepted', boot)
        self.assertNotIn(b'sf/kernel.bin', stage1)

    def test_linux_update_script_checks(self):
        script = (HERE / 'linux' / 'sf-bootbin-install.sh').read_text()
        code = script.split("<<'PY'\n", 1)[1].split('\nPY\n', 1)[0]
        text = (OUT / 'kernel.bin').read_bytes()[32:1200].decode('ascii', 'replace')
        root = text.split('boot-uuid=', 1)[1].split()[0]
        def check(boot, kernel, bench='0', root=root):
            return subprocess.run([sys.executable, '-I', '-B', '-c', code, str(boot), str(kernel), root, bench],
                                  capture_output=True, text=True)
        tmp = pathlib.Path(self.tmp)
        self.assertEqual(check(OUT / 'boot.bin', OUT / 'kernel.bin').returncode, 0)
        self.assertIn('BENCH SESSION', check(OUT / 'boot.bin', OUT / 'kernel-bench.bin', '1').stdout)
        refused = [
            (OUT / 'boot.bin', OUT / 'kernel-bench.bin', "session ends by itself"),          # a bench kernel, unasked
            (OUT / 'boot-bench.bin', OUT / 'kernel.bin', 'not stage 2 alone'),              # records behind the image
            (OUT / 'sf-stage1.bin', OUT / 'kernel.bin', 'do not fit'),                      # too large for stage 1
            (OUT / 'kernel.bin', OUT / 'kernel.bin', 'no m1n1 load header'),
            (OUT / 'boot.bin', OUT / 'boot.bin', 'not a record'),
        ]
        damaged = bytearray((OUT / 'kernel.bin').read_bytes()); damaged[5000] ^= 1
        (tmp / 'damaged.bin').write_bytes(damaged)
        refused.append((OUT / 'boot.bin', tmp / 'damaged.bin', 'checksum'))
        (tmp / 'longer.bin').write_bytes((OUT / 'kernel.bin').read_bytes() + bytes(4))
        refused.append((OUT / 'boot.bin', tmp / 'longer.bin', 'nothing else'))
        for boot, kernel, why in refused:
            result = check(boot, kernel)
            self.assertNotEqual(result.returncode, 0, why)
            self.assertIn(why, result.stderr, why)
        self.assertIn('another root', check(OUT / 'boot.bin', OUT / 'kernel.bin', root='0' * 36).stderr)
        subprocess.run(['bash', '-n', str(HERE / 'linux' / 'sf-bootbin-install.sh')], check=True)

    def test_boot_object_is_stage1_with_the_chainload_line(self):
        stage1 = (OUT / 'sf-stage1.bin').read_bytes()
        obj = open(self.stub.boot_obj_path, 'rb').read()
        tail = ('chosen.asahi,efi-system-partition=%s\nchainload=%s;sf/boot.bin\n' % ((PARTUUID.lower(),) * 2)).encode() + bytes(4)
        self.assertEqual(obj, stage1 + tail)
        # (the installer's extract_vars, used only by its "upgrade m1n1" and "rebuild firmware" actions, which
        # are not on our menu, does not find the lines: this m1n1 has its stack marker more than once)
        self.assertGreater(obj.count(b'STACKBOT'), 1)
        self.assertTrue(m1n1.get_version(self.stub.boot_obj_path).endswith('-siliconforest-stage1-v1'))
        # the bench's test image is the same construction with another identifier
        test = (OUT / 'sf-stage1-test.bin').read_bytes()
        self.assertEqual(test[:len(stage1)], stage1)
        self.assertEqual(len(test), len(obj))

    def test_icon(self):
        icon = open(self.stub.icon_path, 'rb').read()
        self.assertEqual(icon[:4], b'icns')
        self.assertEqual(int.from_bytes(icon[4:8], 'big'), len(icon))
        self.assertEqual(icon, (KIT / 'logo.icns').read_bytes())

    def test_stages(self):
        s1, s2 = (OUT / 'sf-stage1.bin').read_bytes(), (OUT / 'sf-stage2.bin').read_bytes()
        for image in (s1, s2):
            self.assertEqual(image[0x808:0x810], b'AURALOAD')
            self.assertIn(b'SF_STOPPED', image)                 # the installed variant: stops, no proxy, no restart
        self.assertNotIn(b'PDSF: ', s1)                          # stage 1 has no kernel loader
        self.assertIn(b'PDSF: ', s2)
        # only the configuration record differs between kernel.bin and the bench's kernel file
        a, b = (OUT / 'kernel.bin').read_bytes(), (OUT / 'kernel-bench.bin').read_bytes()
        fa, fb = pdsf_pack.parse(a, 0)[0], pdsf_pack.parse(b, 0)[0]
        # the bench's image for a start from RAM: stage 2, the bench's records behind it, four zero bytes
        self.assertEqual((OUT / 'boot-bench.bin').read_bytes(), s2 + b + bytes(4))
        ka, kb = a[fa[1][5]:fa[1][5] + fa[1][2]], b[fb[1][5]:fb[1][5] + fb[1][2]]
        self.assertEqual(hashlib.sha256(ka).digest(), hashlib.sha256(kb).digest())
        ta, tb = a[fa[0][5]:fa[0][5] + fa[0][2]].decode(), b[fb[0][5]:fb[0][5] + fb[0][2]].decode()
        first, rest = ta.split('\n', 1)   # the bench words go at the end of the command line
        self.assertEqual(tb, first + ' sfsession_seconds=150 sfmark=1\n' + rest)
        # sf_bench_kernel.py (a bench copy with another session length) makes the same file for 150 s,
        # and refuses a file that already ends its session
        sys.path.insert(0, str(HERE)); import sf_bench_kernel
        self.assertEqual(sf_bench_kernel.bench_copy(a, 150), b)
        self.assertNotEqual(sf_bench_kernel.bench_copy(a, 470), b)
        self.assertRaises(ValueError, sf_bench_kernel.bench_copy, b, 150)
        self.assertRaises(ValueError, sf_bench_kernel.bench_copy, a, 600)
        args = ta.splitlines()[0].split()
        self.assertIn('sfnvme_ttl=0', args); self.assertIn('rd=uuid', args); self.assertIn('sfnvme_write=1', args)
        for word in args:
            self.assertFalse(word.startswith(('sfbench', 'sfsession_seconds', 'sfmark', 'pdsfstop', 'sfpoweroff', 'sfpmu')), word)

    def test_menu_is_short(self):
        source = (KIT / 'main.py').read_text()
        self.assertIn('actions = {k: v for k, v in actions.items() if k in ("p", "f")}', source)

    def test_manifest(self):
        for line in (OUT / 'mac-kit' / 'MANIFEST.sha256').read_text().splitlines():
            digest, name = line.split('  ', 1)
            self.assertEqual(hashlib.sha256((OUT / 'mac-kit' / name).read_bytes()).hexdigest(), digest, name)


if __name__ == '__main__':
    unittest.main()
