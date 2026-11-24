#!/usr/bin/env python3
"""Host-only native desktop policy and real durable-writer tests; no device."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch
import json
import struct
import tempfile
import unittest

import host_progress as host

N = '0123456789abcdef' * 3
EXPERIMENT = {'version': 1, 'driver': 'PDM1SPIHID', 'input': 'pdhid-keyboard', 'host_hid_injection': False}
MEMBERS = ['org.puredarwin.driver.PDM1SPIHID', 'com.apple.iokit.IOHIDFamily', 'com.ravynos.iokit.IOGOPFramebuffer']


def fileset(ids=(MEMBERS[0],)):
    commands = bytearray()
    for name in ids:
        text = name.encode() + b'\0'
        size = (32 + len(text) + 7) & ~7
        commands += struct.pack('<IIQQII', 0x80000035, size, 0, 0, 32, 0) + text + bytes(size - 32 - len(text))
    return struct.pack('<IiiIIIII', 0xfeedfacf, 0x100000c, 0, 12, len(ids), len(commands), 0, 0) + commands


def inputs(record=None, ids=(MEMBERS[0],), members=MEMBERS, ram=True):
    if record is None:
        record = {'desktop_experiment': {'version': 1, 'command': '/usr/bin/darwin-desktop-test'},
                  'native_keyboard_desktop_experiment': dict(EXPERIMENT)}
    contents = {'kernel': fileset(ids), 'build_record': json.dumps(record).encode()}
    if ram:
        contents['ramdisk'] = b'POLICY_FIXTURE_NOT_A_RUNTIME_IMAGE'
    return SimpleNamespace(contents=contents, report={'kernel': {'members': {x: {} for x in members}}})


class NativePolicy(unittest.TestCase):
    def test_default_off_and_exact_native_mode(self):
        self.assertFalse(host.native_keyboard_desktop_enabled(inputs({})))
        self.assertTrue(host.native_keyboard_desktop_enabled(inputs()))
        with self.assertRaises(ValueError):
            host.native_keyboard_desktop_enabled(inputs({'native_keyboard_desktop_experiment': None}))

    def test_malformed_types_modes_and_source_requirements_rejected(self):
        for key, values in [('version', [True, 0, '1']), ('host_hid_injection', [0, True, 'false']),
                            ('driver', ['', 'PDSerialHID']), ('input', ['pdhid', None])]:
            for value in values:
                record = json.loads(inputs().contents['build_record'])
                record['native_keyboard_desktop_experiment'][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    host.native_keyboard_desktop_enabled(inputs(record))
        for member in MEMBERS:
            with self.subTest(member=member), self.assertRaises(ValueError):
                host.native_keyboard_desktop_enabled(inputs(members=[x for x in MEMBERS if x != member]))
        with self.assertRaises(ValueError):
            host.native_keyboard_desktop_enabled(inputs(members=MEMBERS + ['org.puredarwin.driver.PDSerialHID']))
        for ids in [(), (MEMBERS[0], MEMBERS[0])]:
            with self.subTest(ids=ids), self.assertRaises(ValueError):
                host.native_keyboard_desktop_enabled(inputs(ids=ids))
        with self.assertRaises(ValueError):
            host.native_keyboard_desktop_enabled(inputs(ram=False))

    def test_presence_conflicts_and_duplicate_json_keys_rejected(self):
        for key in ['keyboard_experiment', 'virtual_input_experiment', 'terminal_input_experiment']:
            record = json.loads(inputs().contents['build_record'])
            record[key] = None
            with self.subTest(key=key), self.assertRaises(ValueError):
                host.native_keyboard_desktop_enabled(inputs(record))
        i = inputs()
        i.contents['build_record'] = b'{"x":1,"x":2}'
        with self.assertRaises(ValueError):
            host.native_keyboard_desktop_enabled(i)

    def test_native_geometry_and_exclusive_flags(self):
        geo = dict(width=2560, height=1600, stride=10240, depth=65566)
        host.validate_input_geometry(geo, native_keyboard_desktop=True)
        for kw in [dict(native_keyboard_desktop=1), dict(native_keyboard_desktop=True, terminal_input=True),
                   dict(native_keyboard_desktop=True, virtual_input=True)]:
            with self.subTest(kw=kw), self.assertRaises(ValueError):
                host.validate_input_geometry(geo, **kw)
        with self.assertRaises(ValueError):
            host.validate_input_geometry({**geo, 'width': 1920}, native_keyboard_desktop=True)
        factory = Mock()
        with tempfile.TemporaryDirectory() as tmp:
            for kw in [dict(native_keyboard_desktop=True),
                       dict(native_keyboard_desktop=True, desktop=True, terminal_input=True)]:
                with self.subTest(kw=kw), self.assertRaises(ValueError):
                    host.ConsoleSession('FAKE', Path(tmp), serial_factory=factory, **kw)
        factory.assert_not_called()


class NativeWriter(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.dev = Mock()
        self.dev.write.side_effect = len
        with patch.object(host.threading, 'Thread'):
            self.s = host.ConsoleSession('FAKE', self.directory, serial_factory=lambda *a, **kw: self.dev,
                                         desktop=True, native_keyboard_desktop=True)
        self.s.proof = host.ConsoleProof(N)
        self.s.tx_log = (self.directory / 'tx.bin').open('wb', buffering=0)
        self.addCleanup(self.s.tx_log.close)

    def bootstrap(self):
        self.s.send(b'/bin/sh\r')
        self.s.proof.sent()
        self.s.send(self.s.proof.command())
        self.s.proof.feed(f'\nDL9_BEGIN_{N}\nDarwin\narm64\n25.5.0\nDL9_END_{N}\n'.encode())
        self.assertTrue(self.s.proof.complete)
        self.s.desktop_sent = True

    def test_success_seals_transport_after_exact_launcher_and_instantiates_no_scheduler(self):
        self.assertIsNone(self.s.virtual)
        self.bootstrap()
        launch = host.desktop_command(N)
        self.s.send(launch)
        self.assertEqual(self.s.native_tx_phase, 'sealed')
        before = (self.directory / 'tx.bin').read_bytes()
        for forbidden in [b'\x01k 0 4 0 0 0 0 0\n', launch, b'echo anything\r']:
            with self.assertRaises(ValueError):
                self.s.send(forbidden)
        self.assertEqual((self.directory / 'tx.bin').read_bytes(), before)
        self.assertEqual(self.dev.write.call_count, 3)
        self.assertEqual(before, b'/bin/sh\r' + self.s.proof.command() + launch)

    def test_hid_and_commands_never_permitted_before_bootstrap_or_readiness(self):
        with self.assertRaises(ValueError):
            self.s.send(b'\x01k 0 4 0 0 0 0 0\n')
        self.s.send(self.s.proof.command())
        with self.assertRaises(ValueError):
            self.s.send(host.desktop_command(N))
        self.assertEqual(self.s.native_tx_phase, 'await_launcher')
        self.assertEqual(self.dev.write.call_count, 1)

    def test_short_launcher_write_is_recorded_once_and_cannot_be_retried(self):
        self.bootstrap()
        self.dev.write.side_effect = lambda data: len(data) - 1
        with self.assertRaises(ValueError):
            self.s.send(host.desktop_command(N))
        self.assertEqual(self.s.native_tx_phase, 'sealed')
        count = self.dev.write.call_count
        with self.assertRaises(ValueError):
            self.s.send(host.desktop_command(N))
        self.assertEqual(self.dev.write.call_count, count)
        self.assertTrue((self.directory / 'tx.bin').read_bytes().endswith(host.desktop_command(N)))


if __name__ == '__main__':
    unittest.main()
