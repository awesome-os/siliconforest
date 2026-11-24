"""Malformed artifact and packaging tests. Fixtures are not bootable kernels."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools import m1_firstboot as boot


def kernel_fixture():
    base = 0xfffffe0007004000
    data = bytearray(8192)
    struct.pack_into('<8I', data, 0, 0xfeedfacf, 0x100000c, 0, 2, 3, 384, 0, 0)
    struct.pack_into('<II16s4Q4I', data, 32, 0x19, 72, b'__TEXT_EXEC',
                     base, len(data), 0, len(data), 5, 5, 0, 0)
    struct.pack_into('<4I', data, 104, 5, 288, 6, 68)
    struct.pack_into('<Q', data, 104+272, base+4096)
    struct.pack_into('<I', data, 4096, 0x14000040)
    struct.pack_into('<3I', data, 4352, 0xd5034fdf, 0xd2800013, 0xd5384249)
    struct.pack_into('<2I', data, 4600, 0xd2801c93, 0x14000000)
    marker = boot.CONSOLE_BYTES + (boot.VERSION_TOKEN+'\0').encode()
    data[4608:4608+len(marker)] = marker
    struct.pack_into('<3I', data, 4864, 0xd5034fdf, 0xd2801df3, 0x14000000)
    names = b'\0'
    records = [(b'__start', base+4096), (b'_darwin_lab_m1_first_entry', base+4352)]
    records += [(name, base+4864) for name in boot.UNSUPPORTED_CACHE_SYMBOLS]
    for i, (name, address) in enumerate(records):
        struct.pack_into('<IBBHQ', data, 6144+16*i, len(names), 0x0f, 1, 0, address)
        names += name+b'\0'
    stroff = 6144+16*len(records)
    struct.pack_into('<6I', data, 392, 2, 24, 6144, len(records), stroff, len(names))
    data[stroff:stroff+len(names)] = names
    return bytes(data)


def m1n1_fixture():
    data = bytearray(16384)
    data[0x808:0x810] = b'AURALOAD'
    struct.pack_into('<IIQQ', data, 0x810, 1, 1, 16384, 0)
    tag = ('##m1n1_ver##'+boot.M1N1_PIN+'\0').encode()
    data[0x1000:0x1000+len(tag)] = tag
    return bytes(data)


class MachOTests(unittest.TestCase):
    def test_bounded_entry_is_only_an_observation_of_bytes(self):
        result = boot.inspect_kernel(kernel_fixture())
        self.assertFalse(result['marker_execution_observed'])
        self.assertEqual(result['entry'], 0xfffffe0007005000)

    def test_wrong_architecture_and_fileset_rejected(self):
        for offset, value in [(4, 0x1000007), (12, 12), (8, 2)]:
            data = bytearray(kernel_fixture())
            struct.pack_into('<I', data, offset, value)
            with self.assertRaises(boot.ValidationError):
                boot.inspect_kernel(bytes(data))

    def test_truncated_commands_and_bad_segments_rejected(self):
        variants = [kernel_fixture()[:31], kernel_fixture()[:200]]
        for offset, fmt, value in [(20, '<I', 0xffffffff), (36, '<I', 0),
                                   (80, '<Q', 100000), (104+272, '<Q', 0)]:
            data = bytearray(kernel_fixture())
            struct.pack_into(fmt, data, offset, value)
            variants.append(bytes(data))
        for data in variants:
            with self.assertRaises(boot.ValidationError):
                boot.inspect_kernel(data)

    def test_vm_kernel_identity_or_missing_marker_rejected(self):
        for old in [boot.VERSION_TOKEN.encode(), boot.MARKER.encode()]:
            data = kernel_fixture().replace(old, b'x'*len(old))
            with self.assertRaises(boot.ValidationError):
                boot.inspect_kernel(data)

    def test_marker_cannot_hide_wrong_entry_branch_or_routine(self):
        for offset, instruction in [(4096, 0xd503201f), (4096, 0x14000041),
                                     (4352, 0xd503201f), (4356, 0xd2800000),
                                     (4604, 0xd65f03c0)]:
            data = bytearray(kernel_fixture())
            struct.pack_into('<I', data, offset, instruction)
            with self.assertRaises(boot.ValidationError):
                boot.inspect_kernel(bytes(data))

    def test_unavailable_cache_operations_cannot_return_or_bind_elsewhere(self):
        for offset, fmt, value in [(4864, '<I', 0xd503201f),
                                   (4872, '<I', 0xd65f03c0),
                                   (6144+16*2+8, '<Q', 0xfffffe0007005100)]:
            with self.subTest(offset=offset):
                data = bytearray(kernel_fixture())
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(boot.ValidationError):
                    boot.inspect_kernel(bytes(data))

    def test_relocated_or_unknown_m1n1_header_rejected(self):
        for offset, fmt, value in [(0x810, '<I', 2), (0x814, '<I', 0),
                                   (0x818, '<Q', 1), (0x820, '<Q', 16384)]:
            data = bytearray(m1n1_fixture())
            struct.pack_into(fmt, data, offset, value)
            with self.assertRaises(boot.ValidationError):
                boot.inspect_m1n1(bytes(data))


class PacketTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.manifest = self.root/'input.json'
        contents = {'kernel': kernel_fixture(), 'm1n1': m1n1_fixture(),
                    'source_patch': b'fixture source patch\n',
                    'build_record': b'{"scope":"synthetic fixture"}\n'}
        records = {}
        for name, data in contents.items():
            (self.root/name).write_bytes(data)
            records[name] = {'path': name, 'sha256': hashlib.sha256(data).hexdigest()}
        self.data = {'schema_version': 1, 'target': 'm1-mbp13-j293',
                     'source_pins': {'puredarwin': boot.PUREDARWIN_PIN, 'm1n1': boot.M1N1_PIN},
                     'machine_config': 'T8103', 'kernel_config': 'DEBUG', 'artifacts': records}
        build = {'source_pin': boot.PUREDARWIN_PIN,
                 'patch_sha256': records['source_patch']['sha256'],
                 'kernel_sha256': records['kernel']['sha256'],
                 'package_attribute': boot.PACKAGE_ATTRIBUTE,
                 'machine_config': 'T8103', 'kernel_config': 'DEBUG', 'returncode': 0}
        (self.root/'build_record').write_text(json.dumps(build))
        self.data['artifacts']['build_record']['sha256'] = hashlib.sha256((self.root/'build_record').read_bytes()).hexdigest()
        receipt = {'commit': boot.M1N1_PIN, 'build_tag': boot.M1N1_PIN,
                   'returncode': 0, 'source_clean': True,
                   'files': {'build/m1n1.bin': {'sha256': records['m1n1']['sha256']}}}
        (self.root/'m1n1_build_record').write_text(json.dumps(receipt))
        self.data['artifacts']['m1n1_build_record'] = {'path': 'm1n1_build_record',
            'sha256': hashlib.sha256((self.root/'m1n1_build_record').read_bytes()).hexdigest()}
        reviewed = self.root/'reviewed.json'
        reviewed.write_text(json.dumps({'pin': boot.PUREDARWIN_PIN,
                                        'patch_sha256': records['source_patch']['sha256']}))
        self.addCleanup(patch.stopall)
        patch.object(boot, 'REVIEWED_PATCH_MANIFEST', reviewed).start()
        self.save()

    def save(self):
        self.manifest.write_text(json.dumps(self.data))

    def test_package_preserves_bytes_and_never_permits_execution(self):
        destination = self.root/'packet'
        result = boot.prepare_bundle(self.manifest, destination)
        self.assertFalse(result['execution_permitted'])
        self.assertFalse(result['hardware_boot_observed'])
        self.assertFalse(result['source_and_build_claims_independently_certified'])
        inspected, _ = boot.inspect_manifest(destination/'manifest.json')
        self.assertEqual(inspected['artifacts'], result['artifacts'])

    def test_changed_artifact_rejected_before_creating_packet(self):
        (self.root/'kernel').write_bytes(kernel_fixture()+b'changed')
        destination = self.root/'packet'
        with self.assertRaises(boot.ValidationError):
            boot.prepare_bundle(self.manifest, destination)
        self.assertFalse(destination.exists())

    def test_wrong_board_or_pin_rejected(self):
        for name, value in [('target', 'm1-mini'), ('machine_config', 'VIRT'),
                            ('kernel_config', 'RELEASE'), ('source_pins', {})]:
            original = copy.deepcopy(self.data)
            self.data[name] = value
            self.save()
            with self.assertRaises(boot.ValidationError):
                boot.inspect_manifest(self.manifest)
            self.data = original

    def test_existing_packet_not_overwritten(self):
        destination = self.root/'packet'
        boot.prepare_bundle(self.manifest, destination)
        sentinel = destination/'untouched'
        sentinel.write_text('preserve')
        with self.assertRaises(FileExistsError):
            boot.prepare_bundle(self.manifest, destination)
        self.assertEqual(sentinel.read_text(), 'preserve')

    def test_unreviewed_patch_and_inconsistent_build_receipt_rejected(self):
        for key in ['source_patch', 'build_record', 'm1n1_build_record']:
            with self.subTest(key=key):
                original = (self.root/key).read_bytes()
                original_hash = self.data['artifacts'][key]['sha256']
                changed = b'{}' if key.endswith('record') else b'unreviewed patch'
                (self.root/key).write_bytes(changed)
                self.data['artifacts'][key]['sha256'] = hashlib.sha256(changed).hexdigest()
                self.save()
                with self.assertRaises(boot.ValidationError):
                    boot.inspect_manifest(self.manifest)
                (self.root/key).write_bytes(original)
                self.data['artifacts'][key]['sha256'] = original_hash

    def test_single_wrong_receipt_fact_rejected(self):
        for artifact, key, value in [('build_record', 'kernel_sha256', 'f'*64),
                                     ('build_record', 'patch_sha256', 'f'*64),
                                     ('build_record', 'returncode', False),
                                     ('m1n1_build_record', 'source_clean', False),
                                     ('m1n1_build_record', 'build_tag', '121fcbf')]:
            with self.subTest(artifact=artifact, key=key):
                p = self.root/artifact
                original = p.read_bytes()
                original_hash = self.data['artifacts'][artifact]['sha256']
                receipt = json.loads(original)
                receipt[key] = value
                data = json.dumps(receipt).encode()
                p.write_bytes(data)
                self.data['artifacts'][artifact]['sha256'] = hashlib.sha256(data).hexdigest()
                self.save()
                with self.assertRaises(boot.ValidationError):
                    boot.inspect_manifest(self.manifest)
                p.write_bytes(original)
                self.data['artifacts'][artifact]['sha256'] = original_hash

    def test_missing_hash_and_symlink_artifact_rejected(self):
        self.data['artifacts']['kernel']['sha256'] = ''
        self.save()
        with self.assertRaises(boot.ValidationError):
            boot.inspect_manifest(self.manifest)
        self.data['artifacts']['kernel']['sha256'] = hashlib.sha256(kernel_fixture()).hexdigest()
        (self.root/'linked').symlink_to(self.root/'kernel')
        self.data['artifacts']['kernel']['path'] = 'linked'
        self.save()
        with self.assertRaises(boot.ValidationError):
            boot.inspect_manifest(self.manifest)
