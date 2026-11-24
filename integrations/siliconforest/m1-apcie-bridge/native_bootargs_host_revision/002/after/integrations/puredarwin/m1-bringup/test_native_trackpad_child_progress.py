"""Policy fixtures and real durable writer; no target or injected device events."""
from pathlib import Path
from unittest.mock import Mock, patch
import ast
import json
import tempfile
import unittest
import host_progress as h
import test_native_desktop_progress as native

FIELD = 'native_trackpad_child_experiment'
E = {'version': 1, 'driver': 'PDM1SPIHID', 'child': 'PDM1SPITrackpad',
     'interface': 2, 'mode': 'passive_child', 'input': 'pdhid',
     'feature_writes': False, 'host_hid_injection': False,
     'diagnostic_property': 'PDM1TrackpadChildStateV1'}


def fixture():
    return native.inputs({'desktop_experiment': {'version': 1, 'command': '/usr/bin/darwin-desktop-test'},
                          FIELD: dict(E)})


class ChildPolicy(unittest.TestCase):
    def test_exact_mode_and_legacy_unchanged(self):
        self.assertTrue(h.native_trackpad_child_enabled(fixture()))
        self.assertFalse(h.native_keyboard_desktop_enabled(fixture()))
        self.assertFalse(h.trackpad_enumeration_enabled(fixture()))
        self.assertEqual(h.guest_bootargs(fixture()),
                         h.BOOTARGS + ' pdm1kbd=1 pdm1trackpadenum=1 pdm1trackpadchild=1')
        self.assertFalse(h.native_trackpad_child_enabled(native.inputs()))
        self.assertEqual(h.guest_bootargs(native.inputs()), h.BOOTARGS + ' pdm1kbd=1')

    def test_malformed_types_modes_and_unknown_fields(self):
        cases = {'version': [True, '1', 0], 'interface': [True, 2.0, '2', 1, 3],
                 'feature_writes': [0, True, None], 'host_hid_injection': [0, True, None],
                 'driver': ['PDSerialHID', None], 'child': ['Other', None],
                 'mode': ['enumeration_only', 'enable_mt', None], 'input': ['pdhid-keyboard', None],
                 'diagnostic_property': ['Other', None], 'extra': [False]}
        for key, values in cases.items():
            for value in values:
                i = fixture(); r = json.loads(i.contents['build_record']); r[FIELD][key] = value
                i.contents['build_record'] = json.dumps(r).encode()
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    h.native_trackpad_child_enabled(i)
        for value in [None, False, [], 1]:
            i = fixture(); r = json.loads(i.contents['build_record']); r[FIELD] = value
            i.contents['build_record'] = json.dumps(r).encode()
            with self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)

    def test_presence_conflicts_in_both_directions(self):
        gates = {'keyboard_experiment': h.keyboard_enabled,
                 'virtual_input_experiment': h.virtual_input_enabled,
                 'terminal_input_experiment': h.terminal_input_enabled,
                 'native_keyboard_desktop_experiment': h.native_keyboard_desktop_enabled,
                 'dart_survey_experiment': h.dart_survey_enabled,
                 'trackpad_enumeration_experiment': h.trackpad_enumeration_enabled}
        for key, gate in gates.items():
            for value in [None, False]:
                i = fixture(); r = json.loads(i.contents['build_record']); r[key] = value
                i.contents['build_record'] = json.dumps(r).encode()
                with self.subTest(key=key, value=value):
                    with self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)
                    with self.assertRaises(ValueError): gate(i)
                    with self.assertRaises(ValueError): h.guest_bootargs(i)

    def test_members_ram_desktop_and_actual_spi_entry(self):
        for member in native.MEMBERS:
            i = fixture(); del i.report['kernel']['members'][member]
            with self.subTest(member=member), self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)
        for member in ['org.puredarwin.driver.PDSerialHID', 'org.puredarwin.driver.PDDARTSurvey']:
            i = fixture(); i.report['kernel']['members'][member] = {}
            with self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)
        for ids in [(), (native.MEMBERS[0], native.MEMBERS[0])]:
            i = fixture(); i.contents['kernel'] = native.fileset(ids)
            with self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)
        i = fixture(); del i.contents['ramdisk']
        with self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)
        i = fixture(); r = json.loads(i.contents['build_record']); del r['desktop_experiment']
        i.contents['build_record'] = json.dumps(r).encode()
        with self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)

    def test_duplicate_keys_and_controller_worker_checks(self):
        i = fixture(); i.contents['build_record'] = b'{"native_trackpad_child_experiment":null,"native_trackpad_child_experiment":null}'
        with self.assertRaises(ValueError): h.native_trackpad_child_enabled(i)
        fs = {n.name: n for n in ast.parse(Path(h.__file__).read_text()).body if isinstance(n, ast.FunctionDef)}
        for name in ['verify_inputs', 'guest_bootargs', 'run_worker']:
            self.assertTrue(any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and
                                n.func.id == 'native_trackpad_child_enabled' for n in ast.walk(fs[name])), name)

    def test_geometry_and_constructor_reject_conflicts_before_open(self):
        geo = dict(width=2560, height=1600, stride=10240, depth=65566)
        h.validate_input_geometry(geo, native_trackpad_child=True)
        for kw in [dict(native_trackpad_child=1), dict(native_trackpad_child=True, native_keyboard_desktop=True),
                   dict(native_trackpad_child=True, virtual_input=True)]:
            with self.assertRaises(ValueError): h.validate_input_geometry(geo, **kw)
        with self.assertRaises(ValueError): h.validate_input_geometry({**geo, 'width': 1920}, native_trackpad_child=True)
        factory = Mock()
        with tempfile.TemporaryDirectory() as directory:
            for kw in [dict(native_trackpad_child=True),
                       dict(native_trackpad_child=True, desktop=True, native_keyboard_desktop=True),
                       dict(native_trackpad_child=True, desktop=True, terminal_input=True)]:
                with self.assertRaises(ValueError): h.ConsoleSession('FAKE', Path(directory), serial_factory=factory, **kw)
        factory.assert_not_called()


class ChildWriter(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name); self.dev = Mock(); self.dev.write.side_effect = len
        with patch.object(h.threading, 'Thread'):
            self.s = h.ConsoleSession('FAKE', self.directory, serial_factory=lambda *a, **kw: self.dev,
                                      desktop=True, native_trackpad_child=True)
        self.s.proof = h.ConsoleProof(native.N)
        self.s.tx_log = (self.directory / 'tx.bin').open('wb', buffering=0)
        self.addCleanup(self.s.tx_log.close)

    bootstrap = native.NativeWriter.bootstrap
    test_exact_launcher_seals = native.NativeWriter.test_success_seals_transport_after_exact_launcher_and_instantiates_no_scheduler
    test_no_early_commands = native.NativeWriter.test_hid_and_commands_never_permitted_before_bootstrap_or_readiness
    test_short_write_never_retried = native.NativeWriter.test_short_launcher_write_is_recorded_once_and_cannot_be_retried

    def test_close_records_correct_mode_and_seal(self):
        self.bootstrap(); self.s.send(h.desktop_command(native.N))
        self.s.thread.is_alive.return_value = False
        self.s.close()
        result = json.loads((self.directory / 'console-result.json').read_text())
        self.assertTrue(result['native_trackpad_child_enabled'])
        self.assertFalse(result['native_keyboard_desktop_enabled'])
        self.assertEqual(result['native_mode_host_write_phase'], 'sealed')
        self.assertIsNone(result['error'])
        self.assertIsNone(self.s.virtual)


if __name__ == '__main__': unittest.main()
