"""Snapshot mismatches are rejected. Fixtures are metadata, not a hardware boot."""
import ast
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from io import StringIO

from tools import m1_proxy_preflight as preflight


def snapshot(part=0x23):
    return {
        'schema_version': 1,
        'product': {
            'target_type': 'J293',
            'model': 'MacBookPro17,1',
            'compatible': ['apple,j293', 'apple,t8103', 'apple,arm-platform'],
            'product_name': 'MacBook Pro (13-inch, M1, 2020)',
            'product_description': 'J293',
            'product_soc_name': 'Apple M1',
        },
        'chip': {'adt_chip_id': 0x8103, 'proxy_chip_id': '0x8103'},
        'cpus': [
            {'name': 'cpu0', 'state': 'running', 'cpu_id': 0, 'cluster_id': 0, 'reg': 0x0},
            {'name': 'cpu1', 'state': 'off', 'cpu_id': 1, 'cluster_id': 0, 'reg': 0x1},
        ],
        'uart0': {
            'translated_addr': 0x235200000,
            'size': 0x1000,
            'irq': 521,
            'aapl_phandle': 0x5f,
        },
        'bootargs': {'revision': 2, 'version': 2},
        'memory_map': {
            'SEPFW': {'addr': 0x800000000, 'size': 0x800000},
            'TrustCache': {'addr': 0x800800000, 'size': 0x4000},
        },
        'identity': {
            'build_tag': preflight.PIN,
            'packet_source_pin': preflight.PIN,
            'build_source_pin': preflight.PIN,
            'stage1': 'v1.6.1-dirty',
            'stage2': 'f5a82fdb-dirty',
        },
        'guest': {
            'sctlr': 0xC00800,
            'vpidr': (0x61 << 24) | (0xF << 16) | (part << 4) | 0x1,
        },
        'hardware_tested': False,
    }


def assign(data, path, value):
    cursor = data
    for key in path[:-1]:
        cursor = cursor[key]
    cursor[path[-1]] = value


def remove(data, path):
    cursor = data
    for key in path[:-1]:
        cursor = cursor[key]
    del cursor[path[-1]]


class ObservationTests(unittest.TestCase):
    def test_consistent_claims_stay_observations(self):
        for part, name in ((0x22, 'T8103 Icestorm'), (0x23, 'T8103 Firestorm')):
            with self.subTest(part=hex(part)):
                report = preflight.observe(snapshot(part))
                self.assertEqual(report['status'], preflight.STATUS)
                self.assertNotIn('PASS', report['status'])
                self.assertEqual(report['claim_class'], 'user_or_proxy_metadata')
                self.assertFalse(report['attested'])
                self.assertFalse(report['hardware_boot_observed'])
                self.assertNotIn('execution_permitted', report)
                self.assertNotIn('execution_permitted', json.dumps(report))
                guest = report['observations']['guest']
                self.assertEqual(guest['sctlr_m'], 0)
                self.assertEqual(guest['vpidr_part'], hex(part))
                self.assertEqual(guest['vpidr_part_name'], name)
                self.assertEqual(report['observations']['chip_id'], '0x8103')
                self.assertEqual(report['observations']['running_cpu_count'], 1)
                self.assertEqual(report['observations']['boot_cpu']['name'], 'cpu0')
                self.assertEqual(report['observations']['uart0']['translated_addr'], '0x235200000')
                self.assertEqual(report['observations']['uart0']['size'], 0x1000)
                self.assertFalse(report['observations']['hardware_tested_claim'])
                identity = report['observations']['identity']
                self.assertEqual(identity['build_tag'], preflight.PIN)
                self.assertEqual(identity['packet_source_pin'], preflight.PIN)
                self.assertEqual(identity['build_source_pin'], preflight.PIN)
                self.assertEqual(identity['stage1'], 'v1.6.1-dirty')
                self.assertEqual(identity['stage2'], 'f5a82fdb-dirty')
                self.assertEqual(identity['stage_provenance'], 'inherited_boot_property')
                self.assertFalse(identity['live_image_verification_observed'])

    def test_revisions_one_and_three_and_adjacent_ranges_match(self):
        for revision in (1, 3):
            data = snapshot()
            data['bootargs']['revision'] = revision
            data['memory_map']['TrustCache']['addr'] = 0x800000000 + 0x800000
            self.assertEqual(preflight.observe(data)['observations']['bootargs']['revision'], revision)

    def test_mismatched_identity_board_cpu_uart_ranges_and_guest_rejected(self):
        other = 'f5a82fdb6264edd2fb0c02e2d0f750bf33380854'
        cases = {
            'target J293AP': (('product', 'target_type'), 'J293AP'),
            'model': (('product', 'model'), 'MacBookPro18,3'),
            'missing j293 compatible': (('product', 'compatible'), ['apple,t8103']),
            'missing t8103 compatible': (('product', 'compatible'), ['apple,j293']),
            'hypervisor product mutation': (('product', 'product_name'), 'MacBook Pro on m1n1 hypervisor'),
            'chip disagreement': (('chip', 'proxy_chip_id'), 0x6000),
            'wrong chip': (('chip', 'adt_chip_id'), 0x8112),
            'uart address': (('uart0', 'translated_addr'), 0x235200000 + 0x1000),
            'uart short range': (('uart0', 'size'), 0xFFF),
            'bootargs revision 0': (('bootargs', 'revision'), 0),
            'bootargs revision 4': (('bootargs', 'revision'), 4),
            'sepfw null': (('memory_map', 'SEPFW'), {'addr': 0, 'size': 0x1000}),
            'trustcache empty': (('memory_map', 'TrustCache'), {'addr': 0x1000, 'size': 0}),
            'wrapped range': (('memory_map', 'SEPFW'), {'addr': (1 << 64) - 16, 'size': 32}),
            'overlapping ranges': (('memory_map', 'TrustCache'), {'addr': 0x800001000, 'size': 0x1000}),
            'short build tag': (('identity', 'build_tag'), '121fcbf'),
            'dirty build tag': (('identity', 'build_tag'), preflight.PIN + '-dirty'),
            'old live build tag': (('identity', 'build_tag'), 'v1.6.1'),
            'other packet pin': (('identity', 'packet_source_pin'), other),
            'other build pin': (('identity', 'build_source_pin'), other),
            'marker prefix': (('identity', 'build_tag'), '##m1n1_ver##' + preflight.PIN),
            'sctlr mmu': (('guest', 'sctlr'), 0xC00801),
            'm1 pro part': (('guest', 'vpidr'), (0x61 << 24) | (0x24 << 4)),
            'non-apple implementer': (('guest', 'vpidr'), (0x41 << 24) | (0x23 << 4)),
            't8101 part': (('guest', 'vpidr'), (0x61 << 24) | (0x20 << 4)),
            'hardware tested claim': (('hardware_tested',), True),
        }
        # The chip disagreement case must change only one side. The wrong-chip
        # case changes the ADT side, so set the proxy side to the same wrong id.
        for label, (path, value) in cases.items():
            with self.subTest(label):
                data = snapshot()
                if path == ('chip', 'adt_chip_id'):
                    data['chip']['proxy_chip_id'] = value
                assign(data, path, value)
                with self.assertRaises(preflight.ValidationError):
                    preflight.observe(data)

    def test_two_running_cpus_and_none_running_rejected(self):
        two = snapshot()
        two['cpus'][1]['state'] = 'running'
        none = snapshot()
        none['cpus'][0]['state'] = 'off'
        for data in (two, none, {**snapshot(), 'cpus': []}):
            with self.assertRaises(preflight.ValidationError):
                preflight.observe(data)

    def test_same_wrong_pins_and_differing_pins_rejected(self):
        same = snapshot()
        wrong = 'a' * 40
        for key in ('build_tag', 'packet_source_pin', 'build_source_pin'):
            same['identity'][key] = wrong
        with self.assertRaises(preflight.ValidationError) as same_error:
            preflight.observe(same)
        self.assertIn(preflight.PIN, str(same_error.exception))
        differ = snapshot()
        differ['identity']['build_source_pin'] = 'b' * 40
        with self.assertRaises(preflight.ValidationError) as differ_error:
            preflight.observe(differ)
        self.assertIn('does not match', str(differ_error.exception))

    def test_historical_dirty_stages_with_live_pin_are_provenance(self):
        data = snapshot()
        data['identity']['build_tag'] = preflight.PIN
        data['identity']['packet_source_pin'] = preflight.PIN
        data['identity']['build_source_pin'] = preflight.PIN
        data['identity']['stage1'] = 'v1.6.1-dirty'
        data['identity']['stage2'] = 'f5a82fdb-dirty'
        data['hardware_tested'] = False
        report = preflight.observe(data)
        identity = report['observations']['identity']
        self.assertEqual(report['status'], preflight.STATUS)
        self.assertEqual(identity['build_tag'], preflight.PIN)
        self.assertEqual(identity['stage1'], 'v1.6.1-dirty')
        self.assertEqual(identity['stage2'], 'f5a82fdb-dirty')
        self.assertFalse(identity['live_image_verification_observed'])
        self.assertFalse(report['observations']['hardware_tested_claim'])
        self.assertNotIn('execution_permitted', report)
        self.assertNotIn('PASS', report['status'])

    def test_old_live_build_tag_rejected(self):
        for tag in ('f5a82fdb-dirty', 'v1.6.1-dirty', 'v1.6.1', '121fcbf'):
            with self.subTest(tag):
                data = snapshot()
                data['identity']['stage1'] = 'v1.6.1-dirty'
                data['identity']['stage2'] = 'f5a82fdb-dirty'
                data['identity']['build_tag'] = tag
                with self.assertRaises(preflight.ValidationError) as caught:
                    preflight.observe(data)
                self.assertIn('build_tag', str(caught.exception))

    def test_unbounded_stage_provenance_rejected(self):
        data = snapshot()
        data['identity']['stage2'] = 'f5a82fdb-dirty' + ('x' * 128)
        with self.assertRaises(preflight.ValidationError) as caught:
            preflight.observe(data)
        self.assertIn('stage2', str(caught.exception))

    def test_unhashable_compatible_entry_rejected(self):
        for entry in ({'name': 'apple,t8103'}, ['apple,t8103'], None):
            with self.subTest(entry=type(entry).__name__):
                data = snapshot()
                data['product']['compatible'] = ['apple,j293', entry]
                with self.assertRaises(preflight.ValidationError) as caught:
                    preflight.observe(data)
                self.assertIn('compatible', str(caught.exception))

    def test_absent_hardware_qualification_is_accepted(self):
        data = snapshot()
        del data['hardware_tested']
        report = preflight.observe(data)
        self.assertNotIn('hardware_tested_claim', report['observations'])
        self.assertFalse(report['hardware_boot_observed'])
        self.assertFalse(report['attested'])

    def test_missing_facts_rejected(self):
        paths = [
            ('schema_version',),
            ('product',),
            ('product', 'target_type'),
            ('product', 'model'),
            ('product', 'compatible'),
            ('product', 'product_soc_name'),
            ('chip',),
            ('chip', 'adt_chip_id'),
            ('chip', 'proxy_chip_id'),
            ('cpus',),
            ('uart0',),
            ('uart0', 'translated_addr'),
            ('uart0', 'size'),
            ('bootargs',),
            ('bootargs', 'revision'),
            ('memory_map',),
            ('memory_map', 'SEPFW'),
            ('memory_map', 'TrustCache'),
            ('identity',),
            ('identity', 'build_tag'),
            ('identity', 'packet_source_pin'),
            ('identity', 'build_source_pin'),
            ('identity', 'stage1'),
            ('identity', 'stage2'),
            ('guest',),
            ('guest', 'sctlr'),
            ('guest', 'vpidr'),
        ]
        for path in paths:
            with self.subTest('.'.join(path)):
                data = snapshot()
                remove(data, path)
                with self.assertRaises(preflight.ValidationError) as caught:
                    preflight.observe(data)
                self.assertIn(path[-1], str(caught.exception))

    def test_missing_running_cpu_field_rejected(self):
        for key in ('name', 'state', 'cpu_id', 'cluster_id', 'reg'):
            with self.subTest(key):
                data = snapshot()
                del data['cpus'][0][key]
                with self.assertRaises(preflight.ValidationError) as caught:
                    preflight.observe(data)
                self.assertIn(key, str(caught.exception))

    def test_permission_and_pass_claims_rejected(self):
        for key in ('execution_permitted', 'hardware_boot_pass', 'boot_pass', 'attested'):
            with self.subTest(key):
                data = snapshot()
                data[key] = False
                with self.assertRaises(preflight.ValidationError):
                    preflight.observe(data)

    def test_bool_and_non_object_rejected(self):
        data = snapshot()
        data['chip']['adt_chip_id'] = True
        data['chip']['proxy_chip_id'] = True
        with self.assertRaises(preflight.ValidationError):
            preflight.observe(data)
        with self.assertRaises(preflight.ValidationError):
            preflight.observe([])

    def test_module_imports_no_backend(self):
        tree = ast.parse(Path(preflight.__file__).read_text(encoding='utf-8'))
        imported = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imported.update(alias.name.split('.')[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                imported.add(node.module.split('.')[0])
        self.assertFalse(imported & {
            'subprocess', 'socket', 'serial', 'urllib', 'requests', 'http', 'ctypes', 'os',
        })


class CliTests(unittest.TestCase):
    def test_cli_prints_observations_and_rejects_without_permission(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            good = root / 'snapshot.json'
            good.write_text(json.dumps(snapshot()))
            out = StringIO()
            with patch('sys.stdout', out):
                code = preflight.main(['--snapshot', str(good)])
            self.assertEqual(code, 0)
            report = json.loads(out.getvalue())
            self.assertEqual(report['status'], preflight.STATUS)
            self.assertNotIn('execution_permitted', report)
            self.assertFalse(report['hardware_boot_observed'])

            missing = StringIO()
            with patch('sys.stdout', missing):
                rejected = preflight.main(['--snapshot', str(root / 'absent.json')])
            self.assertEqual(rejected, 1)
            body = json.loads(missing.getvalue())
            self.assertEqual(body['status'], 'REJECTED')
            self.assertNotIn('execution_permitted', body)
            self.assertNotIn('PASS', body['status'])
            self.assertFalse(body['hardware_boot_observed'])

            linked = root / 'linked.json'
            linked.symlink_to(good)
            with patch('sys.stdout', StringIO()):
                self.assertEqual(preflight.main(['--snapshot', str(linked)]), 1)

            duplicate = root / 'duplicate.json'
            duplicate.write_text('{"schema_version": 1, "schema_version": 1}')
            with patch('sys.stdout', StringIO()):
                self.assertEqual(preflight.main(['--snapshot', str(duplicate)]), 1)


if __name__ == '__main__':
    unittest.main()
