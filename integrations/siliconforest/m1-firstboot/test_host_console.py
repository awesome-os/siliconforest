#!/usr/bin/env python3
"""Host-only policy/order/timeout tests. All packets and device objects are fakes.

These tests never import m1n1, open a serial endpoint, or establish hardware proof.
"""
import copy
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2]))
spec = importlib.util.spec_from_file_location('firstboot_host_console', HERE / 'host_console.py')
host = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = host
spec.loader.exec_module(host)
from tests import test_m1_firstboot as fixtures
from tests.test_m1_proxy_preflight import snapshot


def args(directory, **changes):
    values = dict(execute=True, device='/dev/cu.usbmodemHOST_ONLY_FAKE',
                  confirm_recovery=host.CONFIRMATION, private_directory=directory, timeout=1)
    return SimpleNamespace(**(values | changes))


def bootargs():
    return dict(revision=2, version=2, phys_base=0x800000000, virt_base=0xfffffe0000000000,
                mem_size=8 << 30, mem_size_actual=8 << 30, top_of_kernel_data=0x820000000,
                devtree=0xfffffe0002000000, devtree_size=0x8000)


def inputs():
    return host.Inputs({'kernel': host.packet.inspect_kernel(fixtures.kernel_fixture())},
                       {'kernel': fixtures.kernel_fixture(), 'm1n1': fixtures.m1n1_fixture()},
                       {}, {}, '0' * 64)


class InputTests(unittest.TestCase):
    save = fixtures.PacketTests.save

    def setUp(self):
        fixtures.PacketTests.setUp(self)
        self.source = self.root / 'host-source'
        hashes = {}
        for name in host.MANDATORY:
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            data = b'' if name.endswith('__init__.py') else b'# HOST-ONLY FAKE; NEVER IMPORTED\n'
            path.write_bytes(data)
            hashes[name] = host.digest(data)
        receipt_path = self.root / 'm1n1_build_record'
        receipt = json.loads(receipt_path.read_text())
        receipt['proxy_source_sha256'] = hashes
        self.receipt = receipt
        self.update_receipt()
        self.inventory = self.root / 'inventory.json'
        self.inventory.write_text(json.dumps({'target': {
            'identity_confirmed': True, 'observed_compatible': ['apple,j293', 'apple,t8103'],
            'reported_m1n1_stage1_version': 'v1.6.1-dirty',
            'reported_m1n1_stage2_version': 'f5a82fdb-dirty',
        }}))

    def update_receipt(self):
        raw = json.dumps(self.receipt).encode()
        (self.root / 'm1n1_build_record').write_bytes(raw)
        self.data['artifacts']['m1n1_build_record']['sha256'] = host.digest(raw)
        self.save()

    def verify(self):
        return host.verify_inputs(self.manifest, self.source, self.inventory)

    def command(self):
        return ['--manifest', str(self.manifest), '--host-source', str(self.source),
                '--inventory', str(self.inventory)]

    def test_verified_source_bytes_include_empty_modules_and_historical_dirty_tags(self):
        checked = self.verify()
        self.assertEqual(set(checked.host_files), host.MANDATORY)
        self.assertFalse(checked.report['hardware_boot_observed'])
        self.assertEqual(checked.inventory['reported_m1n1_stage2_version'], 'f5a82fdb-dirty')

    def test_extra_source_and_bytecode_are_not_imported_into_verified_tree(self):
        (self.source / 'proxyclient/m1n1/extra.py').write_bytes(b'not pinned')
        (self.source / 'proxyclient/m1n1/__init__.pyc').write_bytes(b'not pinned')
        self.assertEqual(set(self.verify().host_files), host.MANDATORY)

    def test_host_source_mismatch_and_symlink_rejected(self):
        path = self.source / 'proxyclient/m1n1/proxy.py'
        original = path.read_bytes()
        path.write_bytes(original + b'changed')
        with self.assertRaisesRegex(host.Error, 'Host source hash mismatch'):
            self.verify()
        path.unlink()
        target = self.source / 'original'
        target.write_bytes(original)
        path.symlink_to(target)
        with self.assertRaisesRegex(host.Error, 'symlink'):
            self.verify()

    def test_missing_or_unsafe_source_map_rejected(self):
        for change in ('missing', 'traversal', 'bad-hash'):
            original = copy.deepcopy(self.receipt)
            with self.subTest(change):
                if change == 'missing':
                    self.receipt.pop('proxy_source_sha256')
                elif change == 'traversal':
                    self.receipt['proxy_source_sha256']['../outside.py'] = '0' * 64
                else:
                    self.receipt['proxy_source_sha256']['proxyclient/m1n1/proxy.py'] = 'bad'
                self.update_receipt()
                with self.assertRaises(host.Error):
                    self.verify()
            self.receipt = original

    def test_entry_corruption_rejected_before_worker_start_or_log_creation(self):
        raw = bytearray((self.root / 'kernel').read_bytes())
        raw[4096:4100] = b'\x1f\x20\x03\xd5'  # nop, not the reviewed branch
        (self.root / 'kernel').write_bytes(raw)
        self.data['artifacts']['kernel']['sha256'] = host.digest(raw)
        self.save()
        private = self.root / 'private'
        command = self.command() + ['--execute', '--device', '/dev/cu.usbmodemHOST_ONLY_FAKE',
                                   '--confirm-recovery', host.CONFIRMATION, '--private-directory', str(private)]
        with patch.object(host, 'supervise') as worker, patch('sys.stdout', new_callable=io.StringIO):
            self.assertEqual(host.main(command), 1)
            worker.assert_not_called()
        self.assertFalse(private.exists())

    def test_default_and_verified_plan_never_construct_backend_or_worker(self):
        for command in ([], self.command(), self.command() + ['--device', '/dev/cu.usbmodemHOST_ONLY_FAKE']):
            with patch.object(host, 'PinnedBackend') as backend, patch.object(host, 'supervise') as worker:
                with patch('sys.stdout', new_callable=io.StringIO) as output:
                    self.assertEqual(host.main(command), 0)
                self.assertEqual(json.loads(output.getvalue())['status'], 'OFFLINE_PLAN_ONLY')
                backend.assert_not_called()
                worker.assert_not_called()


class ValidationTests(unittest.TestCase):
    def test_execution_gates_reject_before_backend_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            variants = [dict(execute=False), dict(device=None), dict(confirm_recovery=None),
                        dict(confirm_recovery='yes'), dict(device='/dev/cu.debug-console'),
                        dict(device='/dev/cu.usbmodemX:115200'), dict(private_directory=host.LAB / 'private'),
                        dict(timeout=0), dict(timeout=301)]
            for change in variants:
                with self.subTest(change), self.assertRaises(host.Error):
                    factory = Mock()
                    host.run_worker(args(Path(directory), **change), inputs(), factory)
                factory.assert_not_called()
            with patch.dict(host.os.environ, {'M1N1HEAP': '800000000'}), self.assertRaises(host.Error):
                host.authorize(args(Path(directory)))

    def test_bootargs_and_predicted_layout_have_bounded_ranges_and_displacement(self):
        ba = bootargs()
        self.assertEqual(host.validate_bootargs(ba, 0x800100000, 1024), 0x802000000)
        class HexInteger(int):
            pass  # Stand-in for Construct's HexDisplayedInteger, without importing m1n1.
        self.assertEqual(host.validate_bootargs({k: HexInteger(v) for k, v in ba.items()},
                                               0x800100000, 1024), 0x802000000)
        layout = host.validate_layout(ba, snapshot(), 0x840000000, inputs().report['kernel'])
        self.assertEqual((layout['guest_base'] - layout['vmin']) % 0x4000, 0)
        for field, value in [('revision', 4), ('version', 0), ('devtree_size', 0),
                             ('devtree_size', 17 << 20), ('phys_base', 1),
                             ('devtree', (1 << 64) - 1), ('mem_size_actual', 0)]:
            with self.subTest(field=field, value=value), self.assertRaises(host.Error):
                host.validate_bootargs(ba | {field: value}, 0x800100000, 1024)

    def test_layout_rejects_firmware_overlap_ram_overflow_and_misalignment(self):
        ba, kernel = bootargs(), inputs().report['kernel']
        cases = [(ba, snapshot(), 0x840000001, kernel, ()),
                 (ba, snapshot(), 0x9ff000000, kernel, ()),
                 (ba, snapshot(), 0x800100000, kernel, ()),
                 (ba, snapshot(), 0x840000000, kernel, ((0x810000000, 1, 'preoslog'),))]
        bad = snapshot()
        bad['memory_map']['TrustCache']['addr'] = 0x840000000
        cases.append((ba, bad, 0x840000000, kernel, ()))
        unaligned = copy.deepcopy(kernel)
        unaligned['segments'][0]['vmaddr'] += 4
        cases.append((ba, snapshot(), 0x840000000, unaligned, ()))
        for case in cases:
            with self.subTest(case=case), self.assertRaises(host.Error):
                host.validate_layout(*case)

    def test_odd_firmware_lengths_follow_byte_copy_and_page_rounded_placement(self):
        ba, kernel = bootargs(), inputs().report['kernel']
        snap = snapshot()
        sepfw_size, trust_size = 0x1001, 0x4007
        snap['memory_map']['SEPFW'] = {'addr': 0x800000000, 'size': sepfw_size}
        snap['memory_map']['TrustCache'] = {'addr': 0x801000003, 'size': trust_size}
        page = host.PAGE
        layout = host.validate_layout(ba, snap, 0x840000000, kernel)
        vmin = min(segment['vmaddr'] for segment in kernel['segments'])
        span = max(segment['vmaddr'] + segment['vmsize'] for segment in kernel['segments']) - vmin
        phys = 0x840000000 + host.HEAP_RESERVE + host.HOST_HEAP
        guest = phys + (16 << 20) + host.align(ba['devtree_size']) + host.align(trust_size)
        bootargs_off = host.align(span) + host.align(sepfw_size)
        self.assertEqual(layout['phys_base'], phys)
        self.assertEqual(layout['guest_base'], guest)
        self.assertEqual(layout['bootargs_off'], bootargs_off)
        self.assertEqual(layout['entry'], guest + kernel['entry'] - vmin)
        self.assertEqual(layout['vmin'], vmin)
        self.assertEqual((layout['guest_base'] - layout['vmin']) % page, 0)
        self.assertNotEqual(sepfw_size % 8, 0)
        self.assertNotEqual(trust_size % 8, 0)
        self.assertNotEqual(snap['memory_map']['TrustCache']['addr'] % 8, 0)
        logged = host.validate_layout(
            ba, snap, 0x840000000, kernel, ((0x802000000, page, 'preoslog'),))
        self.assertEqual(logged['bootargs_off'], bootargs_off + page)
        self.assertEqual(logged['guest_base'], guest)

    def test_real_collector_rejects_wrong_live_tag_before_feature_query(self):
        backend = host.PinnedBackend.__new__(host.PinnedBackend)
        backend.p = Mock()
        backend.p.get_base.return_value = 0x820000000
        backend.iface = Mock()
        backend.iface.readmem.return_value = b'wrong live tag'
        with self.assertRaisesRegex(host.Error, 'BUILD_TAG'):
            backend.collect(inputs())
        backend.p.get_cpu_features.assert_not_called()
        backend.p.hv_init.assert_not_called()

    def test_real_collector_rejects_target_cpu_features_size_error_before_bootargs(self):
        backend = host.PinnedBackend.__new__(host.PinnedBackend)
        backend.p = Mock()
        backend.p.get_base.return_value = 0x820000000
        backend.p.get_cpu_features.side_effect = ValueError('CPUFeatures target size is not 20')
        backend.iface = Mock()
        backend.iface.readmem.return_value = host.TAG
        with self.assertRaisesRegex(ValueError, 'size is not 20'):
            backend.collect(inputs())
        backend.p.get_bootargs_rev.assert_not_called()
        backend.p.hv_init.assert_not_called()

    def test_real_collector_with_fake_proxy_preserves_live_fields_and_historical_provenance(self):
        class Node(dict):
            __getattr__ = dict.__getitem__

        ba = Node(bootargs())
        facts = snapshot()
        tree = Node(target_type='J293', model='MacBookPro17,1')
        tree['product'] = Node({k: v for k, v in facts['product'].items() if k.startswith('product_')})
        tree['cpus'] = [Node(cpu) for cpu in facts['cpus']]
        mm = SimpleNamespace(**{k: (v['addr'], v['size']) for k, v in facts['memory_map'].items()})
        tree['chosen'] = Node(chip_id=0x8103, **{'memory-map': mm})
        tree['/arm-io/uart0'] = SimpleNamespace(get_reg=lambda _: (0x235200000, 0x4000),
                                              interrupts=[521], **{'AAPL,phandle': 0x5f})
        checked = inputs()
        checked.inventory = dict(observed_compatible=facts['product']['compatible'],
                                 reported_m1n1_stage1_version='historical-dirty-1',
                                 reported_m1n1_stage2_version='historical-dirty-2')
        backend = host.PinnedBackend.__new__(host.PinnedBackend)
        backend.p = Mock()
        backend.p.get_base.return_value = 0x820000000
        backend.p.get_cpu_features.return_value = {'apple_sysregs_unlocked': False}
        backend.p.iodev_whoami.return_value = 4
        backend.p.get_bootargs_rev.return_value = (0x800100000, 2)
        backend.p.get_chipid.return_value = 0x8103
        backend.p.heapblock_alloc.return_value = 0x840000000
        backend.proxy_module = SimpleNamespace(IODEV=SimpleNamespace(USB0=4, USB1=5))
        backend.iface = Mock()
        backend.iface.readmem.side_effect = [host.TAG, b'original fake BootArgs', b'original fake ADT']
        backend.types = SimpleNamespace(BootArgs_r2=SimpleNamespace(sizeof=lambda: 1024, parse=lambda _: ba))
        backend.adt_module = SimpleNamespace(load_adt=lambda _: tree)

        def utils(p, heap_size, m1n1_heap):
            self.assertEqual(heap_size, host.HOST_HEAP)
            return SimpleNamespace(ba=ba, heap_top=0x840000000 + m1n1_heap + heap_size,
                                   cpu_features={'apple_sysregs_unlocked': False},
                                   mrs=lambda name: facts['guest']['sctlr' if name == 'SCTLR_EL12' else 'vpidr'])

        backend.utils_class = utils
        result, raw_ba, raw_adt, layout = backend.collect(checked)
        self.assertEqual(result['identity']['build_tag'], host.packet.M1N1_PIN)
        self.assertEqual(result['identity']['stage2'], 'historical-dirty-2')
        self.assertEqual(result['product']['target_type'], tree.target_type)
        self.assertEqual(result['guest'], facts['guest'])
        self.assertEqual(backend.u.adt_data, raw_adt)
        host.preflight.observe(result)
        backend.p.hv_init.assert_not_called()
        backend.p.reset_panic_counter.assert_not_called()
        # An obvious live identity mismatch must fail even before utility stub writes.
        tree['chosen']['chip_id'] = 0x8101
        backend.iface.readmem.side_effect = [host.TAG, b'original fake BootArgs', b'original fake ADT']
        backend.utils_class = Mock()
        with self.assertRaisesRegex(host.Error, 'selection mismatch'):
            backend.collect(checked)
        backend.utils_class.assert_not_called()


class FakeHV:
    def __init__(self, backend):
        self.backend = backend
        self.smp, self.wdt_cpu, self.emulate_sprr = False, None, False
        self.adt = {'cpus': [SimpleNamespace(state='running')]}
        self.tba = SimpleNamespace()

    def init(self):
        self.backend.events.append('hv.init')
        for name in ('original-adt.bin', 'original-bootargs.bin', 'original-observations.json'):
            if not (self.backend.directory / name).is_file():
                raise AssertionError('metadata was not saved before HV.init')

    def set_bootargs(self, value):
        self.backend.events.append('bootargs')
        self.tba.cmdline = value

    def load_macho(self, data):
        self.backend.events.append('load_macho')
        if data.read() != fixtures.kernel_fixture():
            raise AssertionError('kernel bytes changed')
        for key, value in self.backend.layout.items():
            setattr(self, key, value)
        if self.backend.bad_layout:
            self.guest_base += 4

    def start(self):
        self.backend.events.append('start')


class FakeBackend:
    def __init__(self, checked, directory):
        self.directory = directory
        self.events = []
        self.data = snapshot()
        self.layout = host.validate_layout(bootargs(), self.data, 0x840000000, checked.report['kernel'])
        self.bad_layout = False
        self.late_guest = None
        self.hv = FakeHV(self)

    def open(self, device):
        self.events.append('open')

    def collect(self, checked):
        self.events.append('collect')
        return self.data, b'HOST-ONLY FAKE BOOTARGS', b'HOST-ONLY FAKE ADT', self.layout

    def new_hv(self):
        self.events.append('new_hv')
        return self.hv

    def guest_registers(self):
        self.events.append('guest_registers')
        return self.late_guest or self.data['guest']

    def close(self):
        self.events.append('close')

    def start_guest(self, hv, checked, layout, facts, directory):
        hv.start()  # Capture/transport behavior is tested separately with explicit fakes below.


class OrderingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.checked = inputs()
        self.backend = FakeBackend(self.checked, self.directory)

    def run_fake(self):
        return host.run_worker(args(self.directory), self.checked, lambda *_: self.backend)

    def test_preflight_mismatches_close_host_without_creating_hv(self):
        changes = [('identity', 'build_tag', 'old-dirty'), ('chip', 'proxy_chip_id', 0x8101),
                   ('uart0', 'translated_addr', 0), ('guest', 'sctlr', 1),
                   ('guest', 'vpidr', (0x61 << 24) | (0x24 << 4)),
                   ('product', 'target_type', 'J314')]
        for section, field, value in changes:
            self.backend.data = snapshot()
            self.backend.data[section][field] = value
            self.backend.events.clear()
            with self.subTest(section=section, field=field), self.assertRaises(host.Error):
                self.run_fake()
            self.assertEqual(self.backend.events, ['open', 'collect', 'close'])
        self.backend.data = snapshot()
        self.backend.data['cpus'][1]['state'] = 'running'
        with self.assertRaises(host.Error):
            self.run_fake()
        self.assertNotIn('new_hv', self.backend.events)

    def test_exact_order_preserves_originals_and_never_interprets_return_as_success(self):
        with self.assertRaises(host.BoundedStop):
            self.run_fake()
        self.assertEqual(self.backend.events, ['open', 'collect', 'new_hv', 'hv.init', 'bootargs',
                                              'guest_registers', 'load_macho', 'guest_registers', 'start', 'close'])
        self.assertEqual(self.backend.hv.tba.cmdline, host.BOOTARGS)
        before = json.loads((self.directory / 'before-start.json').read_text())
        self.assertFalse(before['hardware_boot_observed'])
        for path in self.directory.iterdir():
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_guest_mmu_or_identity_change_after_init_prevents_kernel_load(self):
        self.backend.late_guest = {'sctlr': 1, 'vpidr': snapshot()['guest']['vpidr']}
        with self.assertRaises(host.Error):
            self.run_fake()
        self.assertIn('hv.init', self.backend.events)
        self.assertNotIn('load_macho', self.backend.events)
        self.assertNotIn('start', self.backend.events)

    def test_wrong_actual_load_layout_prevents_guest_start(self):
        self.backend.bad_layout = True
        with self.assertRaises(host.Error):
            self.run_fake()
        self.assertIn('load_macho', self.backend.events)
        self.assertNotIn('start', self.backend.events)

    def test_interactive_shell_and_secondary_cpu_escape_are_fatal_not_exception_swallowable(self):
        with self.assertRaises(host.BoundedStop):
            host.stop_guest_control()
        self.assertFalse(issubclass(host.BoundedStop, Exception))


class CaptureTests(unittest.TestCase):
    """Fake framing/context/signal scheduler only; no OS alarm or device I/O."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.events, self.frames = [], []
        self.checked = inputs()
        self.layout = host.validate_layout(bootargs(), snapshot(), 0x840000000, self.checked.report['kernel'])
        self.observed = {'elr': self.layout['guest_base'] + self.checked.report['kernel']['park_address'] - self.layout['vmin'],
                         'spsr': 0x200003c5, 'x0': self.layout['guest_base'] + self.layout['bootargs_off'],
                         'x19': 3, 'cpu_id': 0}
        self.context_bytes = json.dumps(self.observed).encode()
        self.scenario = lambda: self.event(3, 3)
        self.write_action = None

        def parse(raw):
            data = json.loads(raw)
            regs = [0] * 32
            regs[0], regs[19] = data['x0'], data['x19']
            return SimpleNamespace(elr=data['elr'], spsr=data['spsr'], regs=regs, cpu_id=data['cpu_id'])

        self.fmt = SimpleNamespace(parse=parse)
        self.iface = SimpleNamespace(REQ_PROXY=0xaa, REQ_MEMREAD=0xbb, REQ_MEMWRITE=0xcc)
        self.iface.dev = SimpleNamespace(write=self.device_write)
        self.iface.cmd = lambda command, payload=b'': self.iface.dev.write(struct.pack('<I', command) + payload)
        self.iface.reply = self.reply
        self.iface.readmem = self.readmem
        self.iface.writemem = self.writemem
        self.iface.handle_boot = self.handle_boot
        self.iface.handle_event = lambda *a: None
        self.proxy = SimpleNamespace(P_HV_START=0xc02, P_EXIT=1)
        self.hv = SimpleNamespace(started=False, _in_handler=False, _sigint_pending=False,
                                  ctx=None, _info_data=None)
        self.hv.start = self.start
        self.hv.interrupt = lambda: self.iface.dev.write(b'!') if not self.hv._in_handler else None
        self.hv._handle_sigint = self.sigint
        self.hv._load_context = self.load_context
        self.hv.run_shell = lambda *a, **k: self.fail('Original interactive shell reached')
        self.capture = host.ParkCapture(self.hv, self.iface, self.proxy, self.fmt, self.directory,
                                        self.checked.report['kernel'], self.layout, 0)
        self.timers = []
        for name, replacement in [('getitimer', Mock(return_value=(0.0, 0.0))),
                                  ('setitimer', self.setitimer), ('signal', Mock())]:
            patcher = patch.object(host.signal, name, replacement)
            patcher.start()
            self.addCleanup(patcher.stop)

    def setitimer(self, kind, delay):
        self.timers.append(delay)
        if delay:
            self.assertEqual(self.frames[-1], struct.pack('<I7Q', 0xaa, *self.capture.start_args))
            self.assertEqual(self.capture.cmd_depth, 0)
            self.assertEqual(self.capture.io_depth, 0)
            self.assertEqual(self.capture.reply_depth, 1)
            self.events.append('armed after complete frame')

    def device_write(self, data):
        self.events.append(('write', data))
        if self.write_action:
            self.write_action(data)
        self.frames.append(data)
        return len(data)

    def request(self, opcode, *args):
        payload = struct.pack('<7Q', opcode, *(list(args) + [0] * (6 - len(args))))
        self.iface.cmd(self.iface.REQ_PROXY, payload)
        return self.iface.reply(self.iface.REQ_PROXY)

    def start(self):
        self.hv.started = True
        self.request(self.proxy.P_HV_START, self.layout['entry'], self.observed['x0'])

    def sigint(self):
        self.hv._sigint_pending = True
        self.hv.interrupt()

    def reply(self, command):
        if command == self.iface.REQ_PROXY and self.capture.reply_depth == 1:
            self.scenario()
        return b'HOST_ONLY_REPLY'

    def readmem(self, addr, size):
        self.iface.cmd(self.iface.REQ_MEMREAD, b'FAKE READ')
        self.iface.reply(self.iface.REQ_MEMREAD)
        return self.context_bytes

    def writemem(self, addr, data):
        self.iface.cmd(self.iface.REQ_MEMWRITE, b'FAKE WRITE')
        self.iface.dev.write(data)
        self.iface.reply(self.iface.REQ_MEMWRITE)

    def load_context(self):
        self.hv._info_data = self.iface.readmem(0x810000000, 400)
        self.hv.ctx = self.fmt.parse(self.hv._info_data)
        return self.hv.ctx

    def event(self, reason, code, info=0x810000000):
        self.iface.handle_boot(struct.pack('<IIQ', reason, code, info))

    def handle_boot(self, raw):
        reason, code, info = struct.unpack('<IIQ', raw)
        self.hv._in_handler = True
        if code == 4:  # Fake panic/bark path has no ExcInfo load.
            self.hv.run_shell()
        ctx = self.hv._load_context()
        if (reason, code) == (3, 1):
            self.events.append('ordinary hook serviced')
            ctx.regs[0] = 0xdead  # Context commit/mutation must not change saved original bytes.
            self.hv._info_data = b'FAKE COMMITTED CONTEXT'
            self.request(self.proxy.P_EXIT, 2)
            self.hv.ctx = None
            self.hv._in_handler = False
        else:
            self.fail('Unexpected context was allowed to continue')

    def run_capture(self):
        with self.assertRaises(host.BoundedStop):
            self.capture.run()
        self.assertEqual(self.timers[-1], 0)
        self.assertLessEqual(self.timers.count(10), 1)
        self.assertTrue(self.capture.closed)
        return json.loads((self.directory / 'exception-context.json').read_text())

    def injected_context(self, reason=3, code=3):
        self.capture.alarm()
        self.event(reason, code)

    def test_timer_after_full_frame_captures_original_context_without_continuation(self):
        self.scenario = self.injected_context
        result = self.run_capture()
        self.assertTrue(result['expected_park_conditions_match'])
        self.assertEqual(result['observed'], self.observed)
        self.assertEqual((self.directory / 'exception-context.bin').read_bytes(), self.context_bytes)
        self.assertEqual(self.frames.count(b'!'), 1)
        self.assertFalse(result['boot_proof'])
        self.assertFalse(result['hardware_boot_observed'])
        self.assertEqual([f for f in self.frames if len(f) == 60],
                         [struct.pack('<I7Q', 0xaa, *self.capture.start_args)])
        for path in self.directory.iterdir():
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_ordinary_hook_service_before_alarm_does_not_stop_console(self):
        def scenario():
            self.event(3, 1)
            self.assertIn('ordinary hook serviced', self.events)
            self.assertFalse((self.directory / 'exception-context.json').exists())
            self.injected_context()
        self.scenario = scenario
        result = self.run_capture()
        self.assertTrue(result['expected_park_conditions_match'])
        self.assertEqual((self.directory / 'exception-context.bin').read_bytes(), self.context_bytes)
        exits = [f for f in self.frames if len(f) == 60 and struct.unpack_from('<Q', f, 4)[0] == 1]
        self.assertEqual(len(exits), 1)  # Ordinary pre-capture service only.

    def test_unrequested_and_unexpected_contexts_are_diagnostic_not_checkpoint(self):
        for injected, reason, code in [(False, 3, 3), (False, 2, 0), (True, 3, 1), (True, 2, 0)]:
            with self.subTest(injected=injected, reason=reason, code=code):
                self.setUp()
                def scenario():
                    if injected:
                        self.capture.alarm()
                    self.event(reason, code)
                self.scenario = scenario
                result = self.run_capture()
                self.assertFalse(result['expected_park_conditions_match'])
                self.assertEqual(result['observed'], self.observed)

    def test_each_park_condition_is_checked(self):
        for key in ('elr', 'x0', 'x19', 'cpu_id', 'daif', 'mode'):
            with self.subTest(key=key):
                self.setUp()
                observed = self.observed.copy()
                field = 'spsr' if key in ('daif', 'mode') else key
                observed[field] ^= 0x40 if key == 'daif' else 1
                self.context_bytes = json.dumps(observed).encode()
                self.scenario = self.injected_context
                result = self.run_capture()
                self.assertFalse(result['expected_park_conditions_match'])
                self.assertFalse(result['conditions'][field])

    def test_unsafe_alarm_boundaries_never_write_interrupt(self):
        for key, value in [('phase', 'preparing'), ('phase', 'frame-written'), ('reply_depth', 2),
                           ('cmd_depth', 1), ('io_depth', 1), ('dispatch_depth', 1),
                           ('_in_handler', True), ('started', False)]:
            with self.subTest(key=key, value=value):
                self.setUp()
                def scenario():
                    setattr(self.hv if key in ('_in_handler', 'started') else self.capture, key, value)
                    self.capture.alarm()
                self.scenario = scenario
                result = self.run_capture()
                self.assertNotIn(b'!', self.frames)
                self.assertIn('boundary unsafe', result['message'])
                self.assertIsNone(result['observed'])

    def test_alarm_during_partial_start_write_has_no_kick_or_timer(self):
        self.write_action = lambda _: self.capture.alarm()
        result = self.run_capture()
        self.assertNotIn(b'!', self.frames)
        self.assertNotIn(10, self.timers)
        self.assertFalse(result['interrupt_write_completed'])

    def test_alarm_during_bulk_write_or_event_dispatch_has_no_kick(self):
        for operation in ('write', 'event'):
            with self.subTest(operation=operation):
                self.setUp()
                if operation == 'write':
                    def scenario():
                        self.write_action = lambda _: self.capture.alarm()
                        self.iface.writemem(123, b'BULK')
                else:
                    self.iface.handle_event = lambda *a: self.capture.alarm()
                    scenario = lambda: self.iface.handle_event(1, b'event')
                self.scenario = scenario
                self.run_capture()
                self.assertNotIn(b'!', self.frames)

    def test_missing_context_never_reuses_completed_hook_context(self):
        def scenario():
            self.event(3, 1)
            self.event(3, 4)
        self.scenario = scenario
        result = self.run_capture()
        self.assertIsNone(result['observed'])
        self.assertIn('No original', result['context_error'])
        self.assertFalse((self.directory / 'exception-context.bin').exists())

    def test_null_or_malformed_context_stays_error(self):
        for variant in ('null', 'malformed'):
            with self.subTest(variant=variant):
                self.setUp()
                if variant == 'null':
                    self.scenario = lambda: self.event(3, 3, 0)
                else:
                    self.context_bytes = b'INVALID ORIGINAL CONTEXT'
                    self.scenario = self.injected_context
                result = self.run_capture()
                self.assertIsNone(result['observed'])
                self.assertTrue(result['context_error'])
                self.assertFalse(result['expected_park_conditions_match'])
                if variant == 'malformed':
                    self.assertEqual((self.directory / 'exception-context.bin').read_bytes(), self.context_bytes)

    def test_other_known_exception_contexts_are_captured_before_original_callbacks(self):
        for reason, code in [(1, 0), (2, 3), (3, 2), (3, 5), (3, 6), (3, 7)]:
            with self.subTest(reason=reason, code=code):
                self.setUp()
                self.scenario = lambda: self.event(reason, code)
                self.iface.handle_boot = lambda *a: self.fail('Unexpected callback was dispatched')
                result = self.run_capture()
                self.assertEqual(result['observed'], self.observed)
                self.assertFalse(result['expected_park_conditions_match'])

    def test_short_start_write_never_arms_alarm(self):
        self.iface.dev.write = lambda data: len(data) - 1
        result = self.run_capture()
        self.assertNotIn(10, self.timers)
        self.assertIn('Short serial write', result['message'])
        self.assertFalse(result['interrupt_write_completed'])

    def test_short_interrupt_write_is_not_an_injected_capture(self):
        self.iface.dev.write = lambda data: 0 if data == b'!' else len(data)
        self.scenario = self.injected_context
        result = self.run_capture()
        self.assertFalse(result['interrupt_write_completed'])
        self.assertFalse(result['expected_park_conditions_match'])
        self.assertIsNone(result['observed'])

    def test_wrong_start_arguments_reject_before_frame_or_alarm(self):
        def start():
            self.hv.started = True
            self.request(self.proxy.P_HV_START, self.layout['entry'] + 4, self.observed['x0'])
        self.hv.start = start
        result = self.run_capture()
        self.assertEqual(self.frames, [])
        self.assertNotIn(10, self.timers)
        self.assertIn('Unexpected HV_START', result['message'])

    def test_context_read_error_remains_missing_and_preserves_error(self):
        self.iface.readmem = Mock(side_effect=OSError('fake context read failed'))
        self.scenario = self.injected_context
        result = self.run_capture()
        self.assertIsNone(result['observed'])
        self.assertEqual(result['context_error'], 'OSError: fake context read failed')
        self.assertFalse((self.directory / 'exception-context.bin').exists())

    def test_alarm_while_normal_hook_is_dispatching_records_original_without_kick(self):
        original = self.iface.handle_boot
        def hook(raw):
            self.hv._in_handler = True
            self.hv._load_context()
            self.hv.ctx.regs[0] = 0xdead
            self.capture.alarm()
            original(raw)
        self.iface.handle_boot = hook
        self.scenario = lambda: self.event(3, 1)
        result = self.run_capture()
        self.assertEqual(result['observed'], self.observed)
        self.assertNotIn(b'!', self.frames)
        self.assertFalse(result['expected_park_conditions_match'])

    def test_duplicate_alarm_never_sends_second_kick(self):
        def scenario():
            self.capture.alarm()
            self.capture.alarm()
        self.scenario = scenario
        self.run_capture()
        self.assertEqual(self.frames.count(b'!'), 1)

    def test_unexpected_exit_is_blocked_before_command_write(self):
        self.scenario = lambda: self.request(self.proxy.P_EXIT, 2)
        self.run_capture()
        self.assertEqual(len(self.frames), 1)

    def test_start_return_and_read_error_are_not_context_or_boot_success(self):
        for fails in (False, True):
            with self.subTest(fails=fails):
                self.setUp()
                if fails:
                    def scenario():
                        raise OSError('host-only fake transport failed')
                    self.scenario = scenario
                else:
                    self.scenario = lambda: None
                result = self.run_capture()
                self.assertFalse(result['expected_park_conditions_match'])
                self.assertIsNone(result['observed'])

    def test_disk_failure_cannot_return_to_handler_or_allow_later_writes(self):
        self.scenario = self.injected_context
        with patch.object(host, 'write_private', side_effect=OSError('host-only disk error')):
            with self.assertRaises(host.BoundedStop):
                self.capture.run()
        count = len(self.frames)
        with self.assertRaises(host.BoundedStop):
            self.iface.dev.write(b'FORBIDDEN')
        with self.assertRaises(host.BoundedStop):
            self.request(self.proxy.P_EXIT, 2)
        self.assertEqual(len(self.frames), count)
        self.assertEqual(self.timers[-1], 0)

    def test_existing_timer_rejected_without_starting_or_disarming_it(self):
        with patch.object(host.signal, 'getitimer', return_value=(5.0, 0.0)):
            with self.assertRaisesRegex(host.Error, 'already has an alarm'):
                self.capture.run()
        self.assertEqual(self.frames, [])
        self.assertEqual(self.timers, [])


class TimeoutTests(unittest.TestCase):
    def test_timeout_kills_only_host_worker_and_never_reports_boot(self):
        events = []

        class FakeProcess:
            exitcode = None

            def __init__(self, **kwargs):
                self.target = kwargs['target']  # Deliberately never invoked.

            def start(self):
                events.append('host start')

            def join(self, timeout):
                events.append(('host join', timeout))

            def is_alive(self):
                return True

            def kill(self):
                events.append('host kill')
                self.exitcode = -9

        result = host.supervise(args(Path('/tmp/HOST_ONLY_FAKE')), FakeProcess)
        self.assertEqual(events, ['host start', ('host join', 1), 'host kill', ('host join', 5)])
        self.assertEqual(result['status'], 'HOST_WORKER_TIMEOUT')
        self.assertFalse(result['hardware_boot_observed'])

    def test_normal_host_worker_exit_is_not_hardware_success(self):
        proc = Mock(exitcode=0)
        proc.is_alive.return_value = False
        result = host.supervise(args(Path('/tmp/HOST_ONLY_FAKE')), lambda **_: proc)
        proc.kill.assert_not_called()
        self.assertFalse(result['hardware_boot_observed'])
        self.assertEqual(result['status'], 'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT')


if __name__ == '__main__':
    unittest.main(verbosity=2)
