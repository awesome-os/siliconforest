"""Host-only boundaries for the M5 recovery runner. No SSH, serial device, or macvdmtool."""
import json
import os
from pathlib import Path
import pty
import shlex
import stat
import subprocess
import sys
import tempfile
import termios
import time
import unittest
from unittest.mock import patch

from tools import m1_recovery as recovery

BOOT_A = 'a' * 64
BOOT_B = 'b' * 64
LAB = Path(__file__).resolve().parents[1]


def observe(boot=BOOT_A, loader=recovery.LOADER_SHA, fd='1', runtime='30s', reboot='2min',
            kernel=recovery.KERNEL, units='inactive', root='rw,relatime',
            compat=('apple,j293', 'apple,t8103', 'apple,arm-platform')):
    lines = [f'KERNEL={kernel}']
    lines.extend(f'COMPAT={name}' for name in compat)
    lines.append(f'BOOTID={boot}')
    lines.append(f'LOADER={loader}')
    for unit in recovery.UNITS:
        lines.append(f'UNIT_{unit}={units}')
    lines.extend([
        f'RuntimeWatchdogUSec={runtime}', f'RebootWatchdogUSec={reboot}',
        f'WD_FD={fd}', f'ROOTOPT={root}',
    ])
    return '\n'.join(lines)


def sample(directory, **overrides):
    body = {
        'ssh_identity': recovery.IDENTITY,
        'known_hosts': recovery.KNOWN_HOSTS,
        'm1_user': recovery.M1_USER,
        'm1_host': recovery.M1_HOST,
        'expected_kernel': recovery.KERNEL,
        'bootloader_path': recovery.LOADER,
        'bootloader_sha256': recovery.LOADER_SHA,
        'native_tool': recovery.NATIVE,
        'native_tool_sha256': recovery.TOOL_SHA,
        'serial_device': recovery.SERIAL,
        'private_directory': '/tmp/darwin-lab-recovery-private',
    }
    body.update(overrides)
    path = Path(directory) / 'recovery.json'
    path.write_text(json.dumps(body), encoding='utf-8')
    return path


class Clock:
    def __init__(self):
        self.t = 0.0
        self.native_calls = 0
        self.native_args = []
        self.ssh_calls = []
        self.records = []
        self.prior = None
        self.up = True
        self.return_at = None
        self.up_after = None
        self.halt_action = 'timeout'
        self.observations = []
        self.restore_rc = 0
        self.native_error = None
        self.come_back = False
        self.refresh_unlocks = False
        self.neighbor_calls = []
        self.refresh_at = None
        self.reset_at = None
        self.locked = False
        self.unlocked = False
        self.serial = False

    def lock(self):
        self.locked = True

    def unlock(self):
        self.unlocked = True
        self.locked = False

    def serial_begin(self):
        self.serial = True

    def serial_end(self):
        self.serial = False

    def now(self):
        return self.t

    def sleep(self, seconds):
        self.t += float(seconds)

    def reachable(self):
        if self.return_at is not None and self.t >= self.return_at:
            return True
        if self.up_after is not None and self.t >= self.up_after:
            return True
        return self.up

    def last_journal(self):
        return self.prior

    def write(self, record):
        self.records.append(json.loads(json.dumps(record)))

    def native(self, args, timeout):
        recovery.guard_native(self.native_calls, args)
        self.native_calls += 1
        self.native_args.append(list(args))
        if self.native_error:
            raise self.native_error
        self.reset_at = self.t
        self.up = False
        if self.come_back:
            self.return_at = self.t + 25

    def selected_neighbor_refresh(self):
        recovery.require(self.neighbor_calls == [], 'refusing a second neighbor refresh')
        self.neighbor_calls.append(recovery.M1_HOST)
        self.refresh_at = self.t
        if self.refresh_unlocks:
            self.return_at = self.t
        return 0

    def ssh(self, command, timeout):
        self.ssh_calls.append(command)
        if 'systemctl halt' in command:
            if self.halt_action == 'timeout':
                self.up = False
                raise TimeoutError('ssh timed out')
            if self.halt_action == 'stay':
                return 0, ''
            raise AssertionError(self.halt_action)
        if 'RuntimeWatchdogUSec t 0' in command:
            return 0, ''
        if 'set-property' in command:
            return self.restore_rc, ''
        if command.startswith('sudo -n sh -c '):
            if not self.observations:
                raise AssertionError('unexpected observe')
            return 0, self.observations.pop(0)
        raise AssertionError(command)


def cfg(loader=recovery.LOADER_SHA):
    return {'bootloader_sha256': loader, 'private_directory': '/tmp/darwin-lab-recovery-private'}


class ContractTests(unittest.TestCase):
    def test_help_imports_as_a_script(self):
        done = subprocess.run(
            [sys.executable, '-B', 'tools/m1_recovery.py', '--help'],
            cwd=LAB, capture_output=True, text=True, timeout=15, check=False)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn('--execute', done.stdout)
        self.assertNotIn('ModuleNotFoundError', done.stderr)
        offline = json.dumps(recovery.plan())
        self.assertNotIn('PASS', offline)
        self.assertIn(recovery.LOADER_SHA, offline)
        self.assertIn(recovery.CANDIDATE_SHA, offline)
        self.assertIn('m1-controller-008.lock', offline)

    def test_ssh_pins_identity_agent_and_known_hosts(self):
        argv = recovery.ssh_argv('true')
        text = ' '.join(argv)
        for option in ('BatchMode=yes', 'IdentitiesOnly=yes', 'IdentityAgent=none',
                       'StrictHostKeyChecking=yes', 'RequestTTY=no', 'ClearAllForwardings=yes'):
            self.assertIn(option, argv)
        self.assertIn(f'IdentityFile={recovery.IDENTITY}', argv)
        self.assertIn(f'UserKnownHostsFile={recovery.KNOWN_HOSTS}', argv)
        self.assertIn(f'{recovery.M1_USER}@{recovery.M1_HOST}', argv)
        self.assertNotIn('-t', argv)
        self.assertNotIn('id_ed25519', text.replace(recovery.IDENTITY, ''))

    def test_observe_is_one_root_script_and_keeps_compat_lines(self):
        argv = shlex.split(recovery.remote_observe())
        self.assertEqual(argv[:4], ['sudo', '-n', 'sh', '-c'])
        script = argv[4]
        self.assertNotIn('echo COMPAT=$(', script)
        self.assertIn("sed 's/^/COMPAT=/'", script)
        self.assertIn(shlex.quote(recovery.LOADER), script)
        self.assertIn('/proc/1/fd/', script)
        self.assertNotIn(';', recovery.LOADER)
        subprocess.run(['sh', '-n', '-c', script], check=True, timeout=5)
        blob = b'apple,j293\0apple,t8103\0apple,arm-platform\0'
        pipeline = subprocess.run(
            ['sh', '-c', "tr '\\000' '\\n' | sed 's/^/COMPAT=/'"],
            input=blob, capture_output=True, check=True, timeout=5)
        names = recovery.compat_names(pipeline.stdout.decode())
        self.assertEqual(names[:2], ['apple,j293', 'apple,t8103'])
        collapsed = recovery.compat_names('COMPAT=apple,j293 apple,t8103\n')
        self.assertNotIn('apple,j293', collapsed)

    def test_dead_classify_is_gone(self):
        self.assertFalse(hasattr(recovery, 'classify'))

    def test_shared_lock_constant_and_realops_does_not_use_a_per_run_lock(self):
        self.assertEqual(str(recovery.TARGET_LOCK),
                         '/Users/chriskearney/darwin-lab-firstboot-007/m1-controller-008.lock')
        seen = []

        def fake_acquire(path):
            seen.append(Path(path))
            return object()

        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(recovery, 'require_host'), \
                patch.object(recovery, 'prepare_private'), \
                patch.object(recovery, 'acquire_lock', side_effect=fake_acquire):
            ops = recovery.RealOps({'private_directory': tmp, 'bootloader_sha256': recovery.LOADER_SHA})
        self.assertEqual(seen, [recovery.TARGET_LOCK])
        self.assertIsNotNone(ops._lock)

    def test_second_lock_is_refused_and_release_allows_another(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'controller.lock'
            held = recovery.acquire_lock(path)
            script = (
                'import sys\n'
                'from tools.m1_recovery import acquire_lock, Error\n'
                'try:\n'
                '    acquire_lock(sys.argv[1])\n'
                'except Error:\n'
                '    raise SystemExit(19)\n'
            )
            blocked = subprocess.run(
                [sys.executable, '-B', '-c', script, str(path)],
                cwd=LAB, capture_output=True, text=True, timeout=15, check=False)
            self.assertEqual(blocked.returncode, 19, blocked.stderr)
            recovery.release_lock(held)
            again = subprocess.run(
                [sys.executable, '-B', '-c', script, str(path)],
                cwd=LAB, capture_output=True, text=True, timeout=15, check=False)
            self.assertEqual(again.returncode, 0, again.stderr)

    def test_config_accepts_only_exact_reviewed_loader_hashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            for digest in (recovery.LOADER_SHA, recovery.CANDIDATE_SHA):
                path = sample(tmp, bootloader_sha256=digest)
                with patch.object(recovery, 'regular_file', side_effect=lambda item: item), \
                        patch.object(recovery, 'tool_digest', side_effect=AssertionError('read')):
                    loaded = recovery.load_config(path, hash_tool=False)
                self.assertEqual(loaded['bootloader_sha256'], digest)
            path = sample(tmp, bootloader_sha256='e4ead838' + ('0' * 56))
            with self.assertRaises(recovery.Error):
                recovery.load_config(path)
            path = sample(tmp, bootloader_path='/boot/efi/m1n1/boot.bin;id')
            with self.assertRaises(recovery.Error) as caught:
                recovery.load_config(path)
            self.assertIn('bootloader_path', str(caught.exception))

    def test_config_does_not_read_the_private_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = sample(tmp)
            metadata = []

            def regular_file(item):
                metadata.append(str(item))
                return item

            def refuse_key_bytes(self, *args, **kwargs):
                raise AssertionError('private key bytes were read: ' + str(self))

            with patch.object(recovery, 'regular_file', side_effect=regular_file), \
                    patch.object(Path, 'read_bytes', refuse_key_bytes):
                loaded = recovery.load_config(path, hash_tool=False)
            self.assertEqual(loaded['ssh_identity'], recovery.IDENTITY)
            self.assertEqual(loaded['known_hosts'], recovery.KNOWN_HOSTS)
            self.assertEqual(loaded['native_tool'], recovery.NATIVE)
            self.assertEqual(loaded['m1_user'], recovery.M1_USER)
            self.assertEqual(loaded['m1_host'], recovery.M1_HOST)
            self.assertEqual(loaded['bootloader_sha256'], recovery.LOADER_SHA)
            self.assertEqual(metadata, [recovery.IDENTITY, recovery.KNOWN_HOSTS, recovery.NATIVE])

    def test_private_directory_rejects_lab_keydir_and_metacharacters(self):
        with tempfile.TemporaryDirectory() as tmp:
            inside = sample(tmp, private_directory=str(LAB / 'private-recovery'))
            with self.assertRaises(recovery.Error):
                recovery.load_config(inside)
            keyed = sample(tmp, private_directory=str(Path(recovery.IDENTITY).parent / 'logs'))
            with self.assertRaises(recovery.Error):
                recovery.load_config(keyed)
            dirty = sample(tmp, private_directory='/tmp/recovery;rm')
            with self.assertRaises(recovery.Error):
                recovery.load_config(dirty)

    def test_execute_on_this_host_does_not_spawn_ssh(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = sample(tmp, bootloader_sha256=recovery.CANDIDATE_SHA)
            with patch.object(recovery, 'regular_file', side_effect=lambda item: item), \
                    patch.object(recovery, 'tool_digest', return_value=recovery.TOOL_SHA), \
                    patch('tools.m1_recovery.subprocess.run', side_effect=AssertionError('spawn')):
                code = recovery.main(['--config', str(path), '--execute'])
            self.assertEqual(code, 1)


class ObserveTests(unittest.TestCase):
    def test_both_reviewed_loaders_and_rejections(self):
        for digest in (recovery.LOADER_SHA, recovery.CANDIDATE_SHA):
            got = recovery.check_observe(observe(loader=digest), digest)
            self.assertEqual(got['boot_id_sha256'], BOOT_A)
            self.assertEqual(got['runtime_usec'], 30_000_000)
            self.assertEqual(got['reboot_usec'], 120_000_000)
        with self.assertRaises(recovery.Error):
            recovery.check_observe(observe(compat=('apple,t8103',)), recovery.LOADER_SHA)
        with self.assertRaises(recovery.Error):
            recovery.check_observe(observe(kernel='other'), recovery.LOADER_SHA)
        with self.assertRaises(recovery.Error):
            recovery.check_observe(observe(loader=recovery.CANDIDATE_SHA), recovery.LOADER_SHA)
        with self.assertRaises(recovery.Error):
            recovery.check_observe(observe(units='active'), recovery.LOADER_SHA)
        with self.assertRaises(recovery.Error):
            recovery.check_observe(observe(fd='1', runtime='0', reboot='0'), recovery.LOADER_SHA, want_fd=0)
        self.assertEqual(recovery.usec('2min'), 120_000_000)

    def test_native_commands_other_than_one_reboot_serial_are_refused(self):
        for args in (['dfu'], ['poweroff'], ['nop'], ['reboot', 'debugusb'], ['reboot'], []):
            with self.assertRaises(recovery.Error):
                recovery.guard_native(0, args)
        recovery.guard_native(0, ['reboot', 'serial'])
        with self.assertRaises(recovery.Error):
            recovery.guard_native(1, ['reboot', 'serial'])

    def test_execute_host_gate(self):
        recovery.require_host('Darwin', 'arm64', 0)
        for args in (('Linux', 'x86_64', 0), ('Darwin', 'x86_64', 0), ('Darwin', 'arm64', 501)):
            with self.assertRaises(recovery.Error):
                recovery.require_host(*args)


class CycleTests(unittest.TestCase):
    def run_case(self, ops, loader=recovery.LOADER_SHA):
        return recovery.run_cycle(cfg(loader), ops)

    def test_preflight_mismatch_stops_before_watchdogs(self):
        ops = Clock()
        ops.observations = [observe(kernel='nope')]
        record = self.run_case(ops)
        self.assertEqual(record['status'], recovery.ABORTED)
        self.assertEqual(record['phase'], 'pre_halt')
        self.assertFalse(record['watchdogs_restored'])
        self.assertEqual(ops.native_calls, 0)
        self.assertFalse(any('set-property' in item for item in ops.ssh_calls))
        self.assertFalse(any('halt' in item for item in ops.ssh_calls))
        self.assertTrue(ops.unlocked)
        self.assertFalse(ops.serial)
        self.assertNotIn('PASS', record['status'])

    def test_disarm_failure_restores_only_after_readback(self):
        ops = Clock()
        ops.observations = [
            observe(),
            observe(fd='1', runtime='0', reboot='0'),
            observe(),
        ]
        record = self.run_case(ops)
        self.assertEqual(record['status'], recovery.ABORTED)
        self.assertTrue(record['watchdogs_restored'])
        self.assertEqual(ops.native_calls, 0)
        self.assertFalse(any('halt' in item for item in ops.ssh_calls))
        ops.restore_rc = 1
        ops.observations = [observe(), observe(fd='1', runtime='0', reboot='0')]
        failed = self.run_case(ops)
        self.assertFalse(failed['watchdogs_restored'])
        ops.restore_rc = 0
        ops.observations = [
            observe(),
            observe(fd='1', runtime='0', reboot='0'),
            observe(runtime='0', reboot='0'),
        ]
        mismatch = self.run_case(ops)
        self.assertFalse(mismatch['watchdogs_restored'])

    def test_halt_that_stays_up_restores_and_does_not_call_native(self):
        ops = Clock()
        ops.halt_action = 'stay'
        ops.observations = [observe(), observe(fd='0', runtime='0', reboot='0'), observe()]
        record = self.run_case(ops)
        self.assertEqual(record['status'], recovery.ABORTED)
        self.assertEqual(record['phase'], 'halt_requested')
        self.assertTrue(record['watchdogs_restored'])
        self.assertEqual(ops.native_args, [])

    def test_return_during_hold_does_not_reset_or_restore(self):
        ops = Clock()
        ops.up_after = 30
        ops.observations = [observe(), observe(fd='0', runtime='0', reboot='0')]
        record = self.run_case(ops)
        self.assertEqual(record['status'], recovery.UNKNOWN)
        self.assertIn('hold', record['reason'])
        self.assertEqual(ops.native_calls, 0)
        self.assertFalse(record['watchdogs_restored'])
        self.assertFalse(any('RuntimeWatchdogUSec t 30000000' in item for item in ops.ssh_calls))

    def test_native_timeout_after_halt_is_unknown_not_pre_halt(self):
        ops = Clock()
        ops.native_error = TimeoutError('native tool timed out')
        ops.observations = [observe(), observe(fd='0', runtime='0', reboot='0')]
        record = self.run_case(ops)
        self.assertEqual(record['status'], recovery.UNKNOWN)
        self.assertEqual(record['phase'], 'native_requested')
        self.assertEqual(record['native_calls'], 1)
        self.assertEqual(ops.native_args, [['reboot', 'serial']])
        self.assertFalse(record['watchdogs_restored'])
        self.assertEqual(record['reason'], 'native tool timed out; ACE state unknown; no retry')
        self.assertNotEqual(record['status'], recovery.ABORTED)
        again = self.run_case(ops)
        self.assertEqual(again['status'], recovery.UNKNOWN)
        self.assertEqual(ops.native_calls, 1)
        self.assertEqual(len(ops.native_args), 1)

    def test_return_requires_one_neighbor_refresh(self):
        ops = Clock()
        ops.refresh_unlocks = True
        ops.observations = [
            observe(), observe(fd='0', runtime='0', reboot='0'), observe(boot=BOOT_B),
        ]
        seen = []

        def fake_run(argv, timeout, capture_output, text):
            seen.append((list(argv), timeout))
            return subprocess.CompletedProcess(argv, 0, '', '')

        bare = recovery.RealOps.__new__(recovery.RealOps)
        with patch('tools.m1_recovery.subprocess.run', fake_run):
            self.assertEqual(bare.selected_neighbor_refresh(), 0)
            with self.assertRaises(recovery.Error):
                bare.selected_neighbor_refresh()
        record = self.run_case(ops)
        self.assertEqual(record['status'], 'OBSERVATIONS_RECORDED')
        self.assertEqual(ops.native_args, [['reboot', 'serial']])
        self.assertEqual(ops.neighbor_calls, [recovery.M1_HOST])
        self.assertGreaterEqual(ops.refresh_at - ops.reset_at, recovery.NEIGHBOR_WAIT)
        self.assertLess(ops.refresh_at - ops.reset_at, recovery.NEIGHBOR_WAIT + recovery.POLL)
        self.assertEqual(record['neighbor_refresh'], {'attempted': True, 'returncode': 0})
        self.assertEqual(seen, [(['/usr/sbin/arp', '-d', '10.1.1.127'], 5)])
        self.assertIn('StrictHostKeyChecking=yes', recovery.ssh_argv('true'))
        self.assertEqual(record['returned']['boot_id_sha256'], BOOT_B)

    def test_happy_path_one_reset_and_candidate_hash(self):
        ops = Clock()
        ops.come_back = True
        ops.observations = [
            observe(loader=recovery.CANDIDATE_SHA),
            observe(loader=recovery.CANDIDATE_SHA, fd='0', runtime='0', reboot='0'),
            observe(loader=recovery.CANDIDATE_SHA, boot=BOOT_B),
        ]
        record = self.run_case(ops, recovery.CANDIDATE_SHA)
        self.assertEqual(record['status'], 'OBSERVATIONS_RECORDED')
        self.assertFalse(record['hardware_boot_observed'])
        self.assertFalse(record['cpu_halt_independently_observed'])
        self.assertNotIn('PASS', json.dumps(record))
        self.assertEqual(ops.native_args, [['reboot', 'serial']])
        self.assertEqual(record['returned']['boot_id_sha256'], BOOT_B)
        self.assertEqual(record['returned']['runtime_usec'], 30_000_000)
        self.assertTrue(ops.unlocked)

    def test_no_ssh_after_reset_is_unknown_and_journal_blocks_retry(self):
        ops = Clock()
        ops.observations = [observe(), observe(fd='0', runtime='0', reboot='0')]
        record = self.run_case(ops)
        self.assertEqual(record['status'], recovery.UNKNOWN)
        self.assertEqual(record['native_calls'], 1)
        self.assertIn('no SSH', record['reason'])
        ops.prior = record
        ops.native_calls = 0
        blocked = self.run_case(ops)
        self.assertEqual(blocked['native_calls'], 1)
        self.assertEqual(ops.native_args, [['reboot', 'serial']])
        self.assertEqual(ops.ssh_calls.count(recovery.remote_observe()) +
                         sum(1 for item in ops.ssh_calls if 'halt' in item), 3)

    def test_cycle_blocked_matrix(self):
        self.assertFalse(recovery.cycle_blocked(None))
        self.assertFalse(recovery.cycle_blocked({
            'phase': 'halt_requested', 'status': recovery.ABORTED,
            'watchdogs_restored': True, 'native_calls': 0,
        }))
        self.assertTrue(recovery.cycle_blocked({
            'phase': 'halt_requested', 'status': recovery.ABORTED, 'watchdogs_restored': False,
        }))
        self.assertTrue(recovery.cycle_blocked({'phase': 'native_requested', 'native_calls': 1}))
        self.assertTrue(recovery.cycle_blocked({'status': recovery.UNKNOWN, 'phase': 'halt_requested'}))


class JournalIntentTests(unittest.TestCase):
    def test_records_are_ordered_before_halt_and_reset(self):
        ops = Clock()
        ops.come_back = True
        ops.observations = [
            observe(), observe(fd='0', runtime='0', reboot='0'), observe(boot=BOOT_B),
        ]
        order = []
        write_record = ops.write
        call_native = ops.native
        call_ssh = ops.ssh

        def write(record):
            order.append(('journal', record.get('phase'), record.get('native_calls')))
            return write_record(record)

        def native(args, timeout):
            order.append(('native', list(args)))
            return call_native(args, timeout)

        def ssh(command, timeout):
            if 'systemctl halt' in command:
                order.append(('halt',))
            return call_ssh(command, timeout)

        ops.write, ops.native, ops.ssh = write, native, ssh
        record = recovery.run_cycle(cfg(), ops)
        self.assertEqual(record['status'], 'OBSERVATIONS_RECORDED')
        self.assertEqual(order[:4], [
            ('journal', 'halt_requested', 0),
            ('halt',),
            ('journal', 'native_requested', 1),
            ('native', ['reboot', 'serial']),
        ])

    def test_journal_failure_before_native_does_not_reset(self):
        ops = Clock()
        ops.observations = [observe(), observe(fd='0', runtime='0', reboot='0')]

        def write(record):
            if record.get('phase') == 'native_requested':
                raise OSError('journal failed')
            ops.records.append(json.loads(json.dumps(record)))

        ops.write = write
        record = recovery.run_cycle(cfg(), ops)
        self.assertEqual(ops.native_args, [])
        self.assertEqual(record['status'], recovery.UNKNOWN)
        self.assertIn('no reset sent', record['reason'])
        self.assertEqual(ops.records[0]['phase'], 'halt_requested')
        self.assertTrue(any('systemctl halt' in item for item in ops.ssh_calls))
        self.assertLess(ops.native_calls, 1)

    def test_write_pushes_every_byte_then_fsyncs(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = recovery.RealOps.__new__(recovery.RealOps)
            ops.journal = Path(tmp) / 'journal.jsonl'
            body = {'phase': 'native_requested', 'native_calls': 1}
            payload = (json.dumps(body, sort_keys=True) + '\n').encode()
            real_write = os.write
            real_fsync = os.fsync
            seen = []

            def short(fd, data):
                if not data:
                    raise OSError('empty journal write')
                return real_write(fd, data[:1])

            def synced(fd):
                seen.append(ops.journal.read_bytes())
                real_fsync(fd)

            with patch('tools.m1_recovery.os.write', short), patch('tools.m1_recovery.os.fsync', synced):
                ops.write(body)
            self.assertEqual(seen, [payload])
            self.assertEqual(ops.journal.read_bytes(), payload)


class PrivateFileTests(unittest.TestCase):
    def test_private_files_are_0600_and_not_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory = root / 'private'
            recovery.prepare_private(directory)
            self.assertEqual(stat.S_IMODE(directory.lstat().st_mode), 0o700)
            link_dir = root / 'linked'
            link_dir.symlink_to(directory)
            with self.assertRaises(recovery.Error):
                recovery.prepare_private(link_dir)
            log = directory / 'serial.log'
            fd = recovery.open_private(log)
            os.close(fd)
            self.assertEqual(stat.S_IMODE(log.lstat().st_mode), 0o600)
            alias = directory / 'alias.log'
            alias.symlink_to(log)
            with self.assertRaises(recovery.Error):
                recovery.open_private(alias)

    def test_serial_reader_sets_raw_115200_and_joins(self):
        master, slave = pty.openpty()
        try:
            os.set_blocking(slave, False)
            recovery.configure_serial(slave)
            attrs = termios.tcgetattr(slave)
            self.assertEqual(attrs[4], termios.B115200)
            self.assertEqual(attrs[5], termios.B115200)
            self.assertEqual(attrs[3] & (termios.ICANON | termios.ECHO), 0)
            self.assertEqual(recovery.SERIAL_FLAGS & os.O_ACCMODE, os.O_RDONLY)
            self.assertTrue(recovery.SERIAL_FLAGS & os.O_NOCTTY)
            self.assertTrue(recovery.SERIAL_FLAGS & os.O_NONBLOCK)
            with tempfile.TemporaryDirectory() as tmp:
                log = Path(tmp) / 'serial.log'
                stop, thread = recovery.spawn_reader(slave, log)
                slave = -1
                os.write(master, b'console-byte')
                deadline = time.time() + 2
                while time.time() < deadline and (not log.exists() or log.stat().st_size == 0):
                    time.sleep(0.05)
                recovery.stop_reader(stop, thread)
                self.assertFalse(thread.is_alive())
                self.assertEqual(log.read_bytes(), b'console-byte')
                self.assertEqual(stat.S_IMODE(log.stat().st_mode), 0o600)
        finally:
            os.close(master)
            if slave >= 0:
                os.close(slave)


if __name__ == '__main__':
    unittest.main()
