"""Host-only controller tests: synthetic USB, process and target observations."""
import contextlib
import copy
import io
import json
from pathlib import Path
import plistlib
import shlex
import subprocess
import tempfile
from types import SimpleNamespace as NS
import unittest
from unittest.mock import Mock, patch

from tools import m1_kernel_cycle as c


PORT = '/dev/cu.usbmodemFAKE01'
PACKET = {'kernel': {'park_address': 0x1008}, 'artifacts': {'kernel': {'sha256': c.KERNEL_SHA}}}


def endpoint(epoch=0):
    return {'callout': PORT, 'IORegistryEntryID': 101 + epoch, 'sessionID': 201 + epoch}


def linux(boot=1):
    return {'boot_id': f'00000000-0000-4000-8000-{boot:012d}', 'kernel': c.LINUX,
            'loader_sha256': c.LOADER_SHA, 'compatible': ['apple,j293', 'apple,t8103'],
            'mounts': {'/': {'type': 'btrfs', 'options': ['rw']},
                       '/boot': {'type': 'ext4', 'options': ['rw']},
                       '/boot/efi': {'type': 'vfat', 'options': ['rw']}},
            'watchdogs': {'RuntimeWatchdogUSec': '30s', 'RebootWatchdogUSec': '2min'},
            'units': {unit: 'inactive' for unit in c.UNITS}}


def usb(primary=True, secondary=True, location=0x100000):
    children = []
    for interface, port in ((1, PORT), (3, '/dev/cu.usbmodemFAKE03')):
        if (interface == 1 and primary) or (interface == 3 and secondary):
            children.append({'bInterfaceNumber': interface, 'IORegistryEntryChildren': [
                {'IOCalloutDevice': port, 'IODialinDevice': port.replace('/cu.', '/tty.')}]})
    return {'idVendor': 0x1209, 'idProduct': 0x316d, 'locationID': location,
            'IORegistryEntryID': 101, 'sessionID': 201,
            'IORegistryEntryChildren': children}


def guest_evidence(directory):
    layout = {'guest_base': 0x900000000, 'vmin': 0x1000, 'bootargs_off': 0x4000}
    expected = {'elr': layout['guest_base'] + 8, 'x0': layout['guest_base'] + 0x4000,
                'x19': 3, 'spsr': 0x3c5, 'cpu_id': 0}
    raw = b'explicitly fake context; no target bytes'
    context = {'expected_park_conditions_match': True, 'expected': expected, 'observed': dict(expected),
               'event': {'reason': 3, 'code': 3}, 'interrupt_write_completed': True,
               'context_error': None, 'raw_context_sha256': c.sha(raw)}
    records = {'exception-context.json': context, 'before-start.json': {'layout': layout},
               'original-observations.json': {'layout': layout, 'packet': PACKET,
                    'snapshot': {'cpus': [{'state': 'running', 'cpu_id': 0}]}}}
    for name, data in records.items():
        (directory / name).write_text(json.dumps(data))
    (directory / 'exception-context.bin').write_bytes(raw)
    (directory / 'worker.log').write_bytes(b'TTY> ' + c.CHECKPOINT + b'\n')


class FakeOps:
    """No subprocesses, network, device descriptors, or clock waits."""
    def __init__(self, root, fail='', delay=0):
        self.root, self.fail, self.delay = Path(root), fail, delay
        self.events, self.now, self.resets, self.refreshes = [], 0, 0, 0
        self.boot, self.polls, self.phase, self.armed = 1, 0, 'baseline', False
    def clock(self):
        return self.now
    def sleep(self, seconds):
        self.now += seconds
    def start_cycle(self, number):
        self.cycle = self.root / str(number)
        self.cycle.mkdir()
        self.phase = 'baseline'
        self.polls = 0
        self.events.append('start')
        return self.cycle
    def save(self, name, data):
        self.events.append(name)
        if self.fail == name:
            raise OSError('synthetic durable write failure')
        c.durable(self.cycle / name, data)
    def arm(self):
        self.events.append('arm')
        self.armed = True
    def clear(self):
        self.events.append('clear')
        if self.fail == 'clear':
            raise OSError('synthetic latch clear failure')
        self.armed = False
    def observe(self):
        self.events.append('observe')
        if self.phase == 'baseline':
            if self.fail == 'baseline':
                raise ConnectionError('synthetic unavailable baseline')
            return linux(self.boot)
        if self.phase == 'same-linux':
            return linux(self.boot)
        if self.phase != 'return':
            if self.fail == 'bootstrap-discovery-fallback':
                return linux(self.boot + 1)
            raise ConnectionError('synthetic proxy has no Linux SSH')
        if self.now - self.reset_time < self.delay or self.fail == 'return-timeout':
            raise ConnectionError('synthetic recovery delay')
        value = linux(self.boot)
        if self.fail == 'wrong-return':
            value['loader_sha256'] = 'changed'
        return value
    def reboot(self):
        self.events.append('reboot')
        self.phase = 'bootstrap'
        if self.fail == 'reboot':
            self.phase = 'same-linux'
            raise ValueError('synthetic rejected reboot')
    def discover(self):
        self.events.append('discover-' + self.phase)
        if self.phase in ('return', 'same-linux'):
            return None
        if self.fail == self.phase + '-discovery' or self.fail == 'bootstrap-discovery-fallback':
            return None
        if self.phase == 'post-vector' and self.polls < 10:
            self.polls += 1
            return None
        return endpoint(0 if self.phase in ('bootstrap', 'unacked-proxy') else 1)
    def child(self, phase, port):
        self.events.append(phase)
        if self.fail == phase + '-unknown':
            raise c.WorkerUnknown('synthetic worker state unknown')
        directory = self.cycle / phase
        directory.mkdir()
        if phase == 'bootstrap':
            if self.fail != 'no-ack':
                c.durable(directory / 'vector-ack.json', {'status': 'VECTOR_ACKNOWLEDGED_RUNTIME_UNVERIFIED',
                                                        'runtime_verified': False})
            self.phase = 'unacked-proxy' if self.fail == 'no-ack' else 'post-vector'
        else:
            self.phase = 'guest-active'
            guest_evidence(directory)
            if self.fail == 'entry':
                (directory / 'worker.log').write_bytes(b'synthetic missing marker\n')
            if self.fail in ('auto-return', 'delayed-auto-return'):
                self.boot += 1
                self.phase, self.reset_time = 'return', self.now
        return directory
    def reset(self):
        self.events.append('reset')
        self.resets += 1
        if self.fail == 'reset':
            raise c.NativeResetTimeout('synthetic reset timeout')
        self.boot += 1
        self.phase, self.reset_time = 'return', self.now
    def refresh_neighbor(self):
        self.events.append('refresh')
        self.refreshes += 1


class KernelCycleTests(unittest.TestCase):
    def test_offline_default_never_constructs_ops_or_verifies(self):
        with patch.object(c, 'RealOps', side_effect=AssertionError('device Ops')), \
                patch.object(c, 'verify_local', side_effect=AssertionError('file verification')), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(c.main([]), 0)
        self.assertFalse(json.loads(output.getvalue())['device_contacted'])

    def test_execution_rejected_before_ops_without_confirmation_or_host(self):
        for arguments in (['--execute'], ['--execute', '--confirm-recovery', c.CONFIRMATION]):
            with self.subTest(args=arguments), patch.object(c.platform, 'system', return_value='Linux'), \
                    patch.object(c, 'RealOps', side_effect=AssertionError('Ops reached')), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(c.main(arguments), 1)

    def test_cycle_count_bounded(self):
        for count in ('0', '4', '-1'):
            with self.subTest(count=count), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                c.parser().parse_args(['--cycles', count])

    def test_real_offline_source_and_packet_verification(self):
        args = c.parser().parse_args(['--manifest', str(c.LAB / 'artifacts/m1-firstboot-007-r6/manifest.json'),
            '--host-source', str(c.LAB / 'sources/m1n1-baseline'), '--window-image',
            str(c.LAB / 'artifacts/m1-unattended-008/window-candidate-004/m1n1-window.bin')])
        with patch.object(c.subprocess, 'Popen', side_effect=AssertionError('process opened')):
            packet = c.verify_local(args)
        self.assertEqual(packet['artifacts']['kernel']['sha256'], c.KERNEL_SHA)

    def test_helper_drift_refused_before_import(self):
        with patch.object(c, 'read_file', return_value=b'wrong source'), \
                patch.object(c.importlib.util, 'spec_from_file_location', side_effect=AssertionError('import reached')), \
                self.assertRaisesRegex(ValueError, 'Helper source changed'):
            c.verify_local(c.parser().parse_args([]))

    def test_usb_primary_not_secondary_or_dialin(self):
        self.assertEqual(c.primary_cdc(plistlib.dumps([usb()])), endpoint())
        self.assertIsNone(c.primary_cdc(plistlib.dumps([usb(primary=False)])))
        self.assertIsNone(c.primary_cdc(plistlib.dumps([usb(location=0x200000)])))
        device = usb(); device['idProduct'] = 0
        self.assertIsNone(c.primary_cdc(plistlib.dumps([device])))

    def test_usb_empty_and_missing_descendant_properties_are_normal(self):
        for raw in (b'', b'\n ', plistlib.dumps([]), plistlib.dumps([{'idVendor':0x1209,'idProduct':0x316d,'locationID':0x100000}])):
            with self.subTest(raw=raw):
                self.assertIsNone(c.primary_cdc(raw))

    def test_usb_ambiguous_malformed_and_wrong_branch_refused(self):
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            c.primary_cdc(plistlib.dumps([usb(), usb()]))
        device = usb(secondary=False)
        device['IORegistryEntryChildren'][0]['IORegistryEntryChildren'][0]['IOCalloutDevice'] = '/dev/cu.debug-console'
        with self.assertRaisesRegex(ValueError, 'callout'):
            c.primary_cdc(plistlib.dumps([device]))
        device = usb(secondary=False); device['bInterfaceNumber'] = 3
        self.assertIsNone(c.primary_cdc(plistlib.dumps([device])))
        with self.assertRaises(ValueError):
            c.primary_cdc(plistlib.dumps({}))

    def test_usb_requires_device_registry_and_session_identity(self):
        for key in ('IORegistryEntryID', 'sessionID'):
            for value in (None, 0, -1, True, '123', 1 << 64):
                device = usb(); device[key] = value
                if value is None: device.pop(key)
                if value == 1 << 64:
                    device[key] = 0  # plistlib's writer refuses this before the production parser can see it.
                    raw = plistlib.dumps([device]).replace(b'<integer>0</integer>', b'<integer>18446744073709551616</integer>')
                else:
                    raw = plistlib.dumps([device])
                with self.subTest(key=key,value=value), self.assertRaisesRegex(ValueError, 'registry/session identity'):
                    c.primary_cdc(raw)

    def test_postvector_poll_rejects_same_tuple_even_with_changed_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = FakeOps(tmp)
            stale_path = {**endpoint(), 'callout': PORT + 'changed'}
            ops.discover = Mock(side_effect=[endpoint(), stale_path, endpoint(1)])
            self.assertEqual(c.wait_cdc(ops, 25, previous=endpoint()), endpoint(1))
            self.assertEqual(ops.discover.call_count, 3)
            ops.discover = Mock(return_value=stale_path)
            start = ops.clock()
            with self.assertRaises(TimeoutError): c.wait_cdc(ops, 25, previous=endpoint())
            self.assertGreaterEqual(ops.clock() - start, 25)
            self.assertLess(ops.clock() - start, 25.2)

    def test_linux_identity_hash_mounts_services_boot_id(self):
        self.assertEqual(c.check_linux(linux()), linux())
        mutations = [lambda x:x.update(kernel='wrong'), lambda x:x.update(loader_sha256='wrong'),
            lambda x:x.update(compatible=[]), lambda x:x.update(boot_id='bad'),
            lambda x:x['mounts']['/'].update(options=['ro']),
            lambda x:x['mounts']['/boot'].update(options=['ro']),
            lambda x:x['mounts']['/boot/efi'].update(type='ext4'),
            lambda x:x['watchdogs'].update(RuntimeWatchdogUSec='0'),
            lambda x:x['watchdogs'].update(RebootWatchdogUSec='3min'),
            lambda x:x['units'].update({c.UNITS[0]:'active'})]
        for mutation in mutations:
            value = linux(); mutation(value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                c.check_linux(value)
        with self.assertRaisesRegex(ValueError, 'did not change'):
            c.check_linux(linux(), linux()['boot_id'])

    def test_entry_requires_exact_complete_console_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp); guest_evidence(directory)
            self.assertTrue(c.entry_observation(directory, PACKET))
            for raw in (c.CHECKPOINT, b'prefix ' + c.CHECKPOINT + b'\n',
                        c.CHECKPOINT + b' extra\n', b'DARWIN_LAB_M1_EARLY_CONSOLE\n'):
                (directory / 'worker.log').write_bytes(raw)
                self.assertFalse(c.entry_observation(directory, PACKET))

    def test_context_true_alone_cannot_override_wrong_observed_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for change in ('elr', 'x0', 'x19', 'spsr', 'cpu_id', 'flag', 'event', 'hash', 'injection'):
                guest_evidence(directory)
                path = directory / 'exception-context.json'; value = c.read_json(path)
                if change in value['observed']:
                    value['observed'][change] += 1
                elif change == 'flag': value['expected_park_conditions_match'] = 1
                elif change == 'event': value['event']['code'] = 1
                elif change == 'hash': value['raw_context_sha256'] = 'wrong'
                else: value['interrupt_write_completed'] = False
                path.write_text(json.dumps(value))
                with self.subTest(change=change):
                    self.assertFalse(c.entry_observation(directory, PACKET))

    def test_context_packet_binding_and_nzcv_mask(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp); guest_evidence(directory)
            path = directory / 'exception-context.json'; value = c.read_json(path)
            value['observed']['spsr'] |= 0xf0000000
            path.write_text(json.dumps(value))
            self.assertTrue(c.entry_observation(directory, PACKET))
            changed = copy.deepcopy(PACKET); changed['kernel']['park_address'] += 4
            self.assertFalse(c.entry_observation(directory, changed))

    def test_three_cycles_ordered_intents_fresh_poll_and_recovery(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = FakeOps(tmp, delay=35)
            results = c.run_cycles(ops, PACKET, 3)
            self.assertEqual(len(results), 3)
            self.assertEqual(ops.resets, 3); self.assertEqual(ops.refreshes, 3)
            self.assertFalse(ops.armed)
            self.assertGreater(ops.events.count('discover-post-vector'), 3)
            self.assertLess(ops.events.index('baseline.json'), ops.events.index('reboot-intent.json'))
            self.assertLess(ops.events.index('reboot-intent.json'), ops.events.index('reboot'))
            self.assertLess(ops.events.index('reset-intent.json'), ops.events.index('reset'))
            self.assertTrue(all(r['status'] == 'ENTRY_PARK_OBSERVED_LINUX_VERIFIED' for r in results))
            self.assertTrue(all(not r['full_xnu_boot_claimed'] for r in results))

    def test_experiment_failures_recover_once_and_never_repeat(self):
        for failure in ('no-ack', 'entry'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                ops = FakeOps(tmp, fail=failure)
                results = c.run_cycles(ops, PACKET, 3)
                self.assertEqual(len(results), 1)
                self.assertEqual(ops.resets, 1)
                self.assertTrue(results[0]['linux_verified'])
                self.assertNotEqual(results[0]['entry'], 'ENTRY_PARK_OBSERVED')

    def test_no_proxy_attempt_waits_for_fallback_without_reset(self):
        for failure, returned in (('bootstrap-discovery-fallback',True), ('bootstrap-discovery',False), ('reboot',False)):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                ops = FakeOps(tmp, fail=failure)
                results = c.run_cycles(ops, PACKET, 3)
                self.assertEqual(len(results), 1)
                self.assertEqual(ops.resets, 0)
                self.assertEqual(results[0]['linux_verified'], returned)
                self.assertNotIn('bootstrap', ops.events)

    def test_guest_autonomous_linux_return_is_verified_without_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = FakeOps(tmp, fail='auto-return')
            result = c.run_cycles(ops, PACKET, 1)[0]
            self.assertEqual(result['status'], 'ENTRY_PARK_OBSERVED_LINUX_VERIFIED')
            self.assertEqual(ops.resets, 0)
            self.assertNotIn('reset-intent.json', ops.events)

    def test_first_ssh_failure_cdc_absent_later_linux_gets_no_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = FakeOps(tmp, fail='delayed-auto-return', delay=35)
            result = c.run_cycles(ops, PACKET, 1)[0]
            self.assertEqual(result['status'], 'ENTRY_PARK_OBSERVED_LINUX_VERIFIED')
            self.assertEqual(ops.resets, 0); self.assertEqual(ops.refreshes, 1)
            self.assertIsNone(c.read_json(ops.cycle / 'recovery-cdc.json')['primary_cdc'])
            self.assertNotIn('reset-intent.json', ops.events)

    def test_first_ssh_failure_cdc_present_permits_exactly_one_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = FakeOps(tmp)
            result = c.run_cycles(ops, PACKET, 1)[0]
            self.assertEqual(result['status'], 'ENTRY_PARK_OBSERVED_LINUX_VERIFIED')
            self.assertEqual(ops.resets, 1)
            self.assertLess(ops.events.index('discover-guest-active'), ops.events.index('reset-intent.json'))
            intent = c.read_json(ops.cycle / 'reset-intent.json')
            self.assertEqual(intent['primary_cdc'], endpoint(1))

    def test_missing_postvector_cdc_and_linux_stops_without_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = FakeOps(tmp, fail='post-vector-discovery')
            result = c.run_cycles(ops, PACKET, 3)
            self.assertEqual(len(result), 1)
            self.assertEqual(result[0]['status'], 'RECOVERY_INCOMPLETE_NO_RETRY')
            self.assertEqual(ops.resets, 0); self.assertEqual(ops.refreshes, 1)
            self.assertTrue(ops.armed)
            self.assertNotIn('guest', ops.events)

    def test_remote_observation_script_compiles_and_preserves_baseline(self):
        ops = c.RealOps(c.parser().parse_args([]))
        ops.ssh = Mock(return_value=NS(returncode=0, stdout=json.dumps(linux()).encode()))
        self.assertEqual(ops.observe(), linux())
        command = shlex.split(ops.ssh.call_args.args[0])
        self.assertEqual(command[:4], ['sudo','-n','python3','-c'])
        compile(command[4], '<fake-remote-observer>', 'exec')
        self.assertIn('RuntimeWatchdogUSec', command[4])
        self.assertIn("'/boot'", command[4])

    def test_worker_unknown_blocks_reset_and_all_further_hardware_calls(self):
        for failure in ('bootstrap-unknown', 'guest-unknown'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                ops = FakeOps(tmp, fail=failure)
                result = c.run_cycles(ops, PACKET, 3)[0]
                self.assertEqual(result['status'], 'WORKER_STATE_UNKNOWN_NO_RESET')
                self.assertEqual(ops.resets, 0); self.assertEqual(ops.refreshes, 0)
                self.assertTrue(ops.armed)
                self.assertEqual(ops.events[-1], 'result.json')

    def test_baseline_and_reboot_intent_failure_prevent_reboot_reset(self):
        for failure in ('baseline', 'baseline.json', 'reboot-intent.json'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                ops = FakeOps(tmp, fail=failure)
                c.run_cycles(ops, PACKET, 3)
                self.assertNotIn('reboot', ops.events)
                self.assertEqual(ops.resets, 0)

    def test_reset_intent_failure_prevents_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = FakeOps(tmp, fail='reset-intent.json')
            result = c.run_cycles(ops, PACKET, 3)[0]
            self.assertEqual(result['status'], 'RECOVERY_INCOMPLETE_NO_RETRY')
            self.assertEqual(ops.resets, 0)

    def test_failed_reset_return_or_latch_clear_never_retries(self):
        for failure in ('reset', 'wrong-return', 'return-timeout', 'clear'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                ops = FakeOps(tmp, fail=failure)
                result = c.run_cycles(ops, PACKET, 3)
                self.assertEqual(len(result), 1)
                self.assertEqual(ops.resets, 1)
                self.assertEqual(result[0]['status'], 'RECOVERY_INCOMPLETE_NO_RETRY')
                if failure == 'reset':
                    self.assertTrue(result[0]['native_tool_killed'])
                    self.assertEqual(result[0]['ace_state'], 'unknown')
                self.assertTrue(ops.armed)
                self.assertLessEqual(ops.refreshes, 1)

    def test_outer_cli_timeout_never_kills_supervisor_or_assumes_worker_exit(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = c.RealOps(c.parser().parse_args([])); ops.cycle = Path(tmp); ops.lock_fd = 123
            proc = Mock(pid=456); proc.wait.side_effect = subprocess.TimeoutExpired('fake', 150)
            with patch.object(c, 'verify_local', return_value=PACKET), patch.object(c.subprocess, 'Popen', return_value=proc) as start:
                with self.assertRaises(c.WorkerUnknown): ops.child('bootstrap', PORT)
            proc.kill.assert_not_called(); proc.terminate.assert_not_called()
            self.assertEqual(start.call_args.kwargs['pass_fds'], (123,))
            self.assertTrue(start.call_args.kwargs['start_new_session'])
            self.assertIn('--timeout', start.call_args.args[0])

    def test_native_timeout_records_killed_tool_and_unknown_ace_without_retry(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = c.RealOps(c.parser().parse_args([])); ops.cycle = Path(tmp)
            with patch.object(c, 'read_file', return_value=b'fake native'), \
                    patch.object(c, 'NATIVE_SHA', c.sha(b'fake native')), \
                    patch.object(c.subprocess, 'run', side_effect=subprocess.TimeoutExpired('fake native',12)) as run:
                with self.assertRaises(c.NativeResetTimeout): ops.reset()
            self.assertEqual(run.call_count, 1)
            record = c.read_json(Path(tmp) / 'native-reset-timeout.json')
            self.assertTrue(record['native_tool_killed'])
            self.assertEqual(record['ace_state'], 'unknown')
            self.assertFalse(record['retry_permitted'])

    def test_child_requires_nonnull_worker_exit_record(self):
        for exit_code in (None, True, 1):
            with self.subTest(exit_code=exit_code), tempfile.TemporaryDirectory() as tmp:
                ops = c.RealOps(c.parser().parse_args([])); ops.cycle = Path(tmp); ops.lock_fd = 123
                directory = Path(tmp) / 'guest'; directory.mkdir()
                c.durable(directory / 'host-result.json', {'worker_returncode': exit_code,
                          'status':'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT'})
                proc = Mock(pid=456); proc.wait.return_value = 1
                with patch.object(c, 'verify_local', return_value=PACKET), patch.object(c.subprocess, 'Popen', return_value=proc):
                    if type(exit_code) is int:
                        self.assertEqual(ops.child('guest', PORT), directory)
                    else:
                        with self.assertRaises(c.WorkerUnknown): ops.child('guest', PORT)

    def test_fixed_transport_commands_and_shared_lock(self):
        from tools import m1_recovery
        self.assertEqual(c.LOCK, m1_recovery.TARGET_LOCK)
        ops = c.RealOps(c.parser().parse_args([])); ops.known_hosts_sha = c.sha(b'fake known hosts')
        ops.command = Mock(return_value=NS(returncode=0, stdout=b''))
        with patch.object(c, 'read_file', return_value=b'fake known hosts'):
            ops.ssh('true')
        argv = ops.command.call_args.args[1]
        self.assertIn('StrictHostKeyChecking=yes', argv)
        self.assertIn('UpdateHostKeys=no', argv)
        self.assertEqual(argv[-2:], ['chrisk@10.1.1.127', 'true'])
        ops.discover()
        self.assertEqual(ops.command.call_args.args[1], ['/usr/sbin/ioreg', '-a', '-r', '-l', '-c', 'IOUSBHostDevice'])
        ops.refresh_neighbor()
        self.assertEqual(ops.command.call_args.args[1], ['/usr/sbin/arp', '-d', '10.1.1.127'])

    def test_durable_private_record_exclusive_and_fsynced(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(c.os, 'fsync', wraps=c.os.fsync) as sync:
            path = Path(tmp) / 'intent.json'
            c.durable(path, {'intent': True})
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(sync.call_count, 2)
            with self.assertRaises(FileExistsError): c.durable(path, {})


if __name__ == '__main__':
    unittest.main()
