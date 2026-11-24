"""Host-only continuing-controller recovery and raw console verification tests."""
import contextlib
import io
import importlib.util
import importlib.machinery
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from tools import m1_bringup_cycle as c
from tests.test_m1_kernel_cycle import FakeOps


CANDIDATE = {'artifacts': {'kernel': 'f'*64}}


class Ops(FakeOps):
    def __init__(self, *args, guest_host_status='HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT',
                 guest_worker_returncode=1, diagnostics_complete=False, **kwargs):
        super().__init__(*args, **kwargs)
        self.args = SimpleNamespace(launcher_sha256='a'*64, inspector_sha256='b'*64)
        self.guest_host_status = guest_host_status
        self.guest_worker_returncode = guest_worker_returncode
        self.diagnostics_complete = diagnostics_complete

    def child(self, phase, port):
        directory = super().child(phase, port)
        # Continuing workers produce their own termination receipts. Keep
        # these separate from the frozen fake's diagnostic-entry fixture.
        result = (dict(status='BOOTSTRAP_WORKER_EXITED_NO_RUNTIME_VERDICT', worker_returncode=1)
                  if phase == 'bootstrap' else
                  dict(status=self.guest_host_status, worker_returncode=self.guest_worker_returncode))
        (directory/'host-result.json').write_text(json.dumps(result))
        if phase == 'guest':
            (directory/'exception-context.json').write_text(json.dumps(dict(
                status='CONTINUING_XNU_STOPPED_FOR_OBSERVATION',
                diagnostics_complete=self.diagnostics_complete, hardware_boot_observed=False)))
        return directory


class BringupCycleTests(unittest.TestCase):
    def test_continuing_sources_and_frozen_sources_have_the_bound_bytes(self):
        for name, expected in {**c.base.SOURCE_SHA, **c.CONTINUING_SOURCES,
                               'tools/m1_kernel_cycle.py': c.BASE_SHA, c.INPUT_HELPER: c.INPUT_HELPER_SHA,
                               c.TERMINAL_HELPER: c.TERMINAL_HELPER_SHA}.items():
            with self.subTest(source=name):
                self.assertEqual(c.base.sha(c.base.read_file(c.LAB/name)), expected)

    def test_source_drift_refuses_before_packet_import_or_child(self):
        args = SimpleNamespace(
            launcher_sha256=c.base.sha(c.base.read_file(c.LAB/c.LAUNCHER)),
            inspector_sha256=c.base.sha(c.base.read_file(c.LAB/c.INSPECTOR)))
        original_read = c.base.read_file
        for source in (c.LAUNCHER, c.INSPECTOR, c.PROGRESS_BOOTSTRAP,
                       c.BOOTARGS_HELPER, c.INPUT_HELPER, c.TERMINAL_HELPER, 'tools/m1_kernel_cycle.py'):
            def drift(path, *args):
                raw = original_read(path, *args)
                return raw + b'\n# HOST-ONLY DRIFT\n' if Path(path) == c.LAB/source else raw
            with self.subTest(source=source), patch.object(c.base, 'read_file', side_effect=drift), \
                    patch.object(c.base, 'verify_local') as old_verify, \
                    patch.object(c.importlib.util, 'spec_from_file_location') as load, \
                    patch.object(c.subprocess, 'Popen') as launch, self.assertRaises(ValueError):
                c.verify(args)
            old_verify.assert_not_called()
            load.assert_not_called()
            launch.assert_not_called()

    def test_missing_continuing_hash_cannot_bypass_binding(self):
        args = SimpleNamespace(
            launcher_sha256=c.base.sha(c.base.read_file(c.LAB/c.LAUNCHER)),
            inspector_sha256=c.base.sha(c.base.read_file(c.LAB/c.INSPECTOR)))
        for removed in (c.PROGRESS_BOOTSTRAP, c.BOOTARGS_HELPER):
            hashes = {k: v for k, v in c.CONTINUING_SOURCES.items() if k != removed}
            with self.subTest(removed=removed), patch.object(c, 'CONTINUING_SOURCES', hashes), \
                    patch.object(c.base, 'verify_local') as old_verify, \
                    self.assertRaisesRegex(ValueError, 'sources must be bound'):
                c.verify(args)
            old_verify.assert_not_called()

    def test_new_bootstrap_preflight_failure_precedes_candidate_import(self):
        args = SimpleNamespace(
            launcher_sha256=c.base.sha(c.base.read_file(c.LAB/c.LAUNCHER)),
            inspector_sha256=c.base.sha(c.base.read_file(c.LAB/c.INSPECTOR)))
        check = Mock(side_effect=ValueError('synthetic missing frozen trampoline'))
        loader = Mock()
        loader.exec_module.side_effect = lambda module: setattr(module, 'verify', check)
        spec = importlib.machinery.ModuleSpec('host_only_fake_bootstrap', loader)
        with patch.object(c.base, 'verify_local', return_value={}) as old_verify, \
                patch.object(c.importlib.util, 'spec_from_file_location', return_value=spec) as load, \
                patch.object(c.subprocess, 'Popen') as launch, \
                self.assertRaisesRegex(ValueError, 'missing frozen trampoline'):
            c.verify(args)
        old_verify.assert_called_once_with(args)
        check.assert_called_once_with(args)
        load.assert_called_once_with('bound_continuing_bootstrap', c.LAB/c.PROGRESS_BOOTSTRAP)
        launch.assert_not_called()

    def child_ops(self, root):
        # Do not enter RealOps: no real lock, SSH, IORegistry, or device access.
        ops = c.Ops.__new__(c.Ops)
        ops.cycle = Path(root)
        ops.lock_fd = 123  # Fake Popen receives this value; no OS call uses it.
        ops.args = SimpleNamespace(manifest=Path('/host-only/manifest'),
            host_source=Path('/host-only/pinned-source'), inventory=Path('/host-only/inventory'),
            window_image=Path('/host-only/window.bin'))
        return ops

    def test_bootstrap_launch_rechecks_then_uses_fixed_copy_deadlines_and_lock(self):
        with tempfile.TemporaryDirectory() as root:
            ops = self.child_ops(root)
            process = Mock(pid=4321)
            events = []
            def start(argv, **kwargs):
                events.append('launch')
                self.assertFalse((ops.cycle/'bootstrap').exists())
                self.assertFalse(kwargs.get('shell', False))
                return process
            def wait(*, timeout):
                self.assertTrue((ops.cycle/'bootstrap-pid.json').is_file())
                directory = ops.cycle/'bootstrap'
                directory.mkdir()
                (directory/'host-result.json').write_text(json.dumps(dict(
                    status='BOOTSTRAP_WORKER_EXITED_NO_RUNTIME_VERDICT', worker_returncode=1)))
                return 1
            process.wait.side_effect = wait
            with patch.object(c, 'verify', side_effect=lambda args: events.append('verify')) as verify, \
                    patch.object(c.subprocess, 'Popen', side_effect=start) as launch:
                directory = ops.child('bootstrap', '/dev/cu.usbmodemFAKE01')
            self.assertEqual(events, ['verify', 'launch'])
            verify.assert_called_once_with(ops.args)
            argv = launch.call_args.args[0]
            self.assertEqual(argv, [c.sys.executable, '-I', str(c.LAB/c.PROGRESS_BOOTSTRAP),
                '--manifest', str(ops.args.manifest), '--host-source', str(ops.args.host_source),
                '--inventory', str(ops.args.inventory), '--execute', '--device', '/dev/cu.usbmodemFAKE01',
                '--private-directory', str(ops.cycle/'bootstrap'), '--confirm-recovery', c.base.CONFIRMATION,
                '--timeout', '120', '--window-image', str(ops.args.window_image)])
            self.assertEqual(launch.call_args.kwargs['pass_fds'], (ops.lock_fd,))
            self.assertIs(launch.call_args.kwargs['start_new_session'], True)
            self.assertEqual(launch.call_args.kwargs['stdin'], c.subprocess.DEVNULL)
            self.assertEqual(launch.call_args.kwargs['stderr'], c.subprocess.STDOUT)
            process.wait.assert_called_once_with(timeout=150)
            process.kill.assert_not_called()
            process.terminate.assert_not_called()
            self.assertEqual(directory, ops.cycle/'bootstrap')
            self.assertTrue((ops.cycle/'bootstrap-completed.json').is_file())
            self.assertFalse((directory/'vector-ack.json').exists())  # Completion is not vector/boot proof.

    def test_bootstrap_recheck_failure_never_opens_child_or_log(self):
        with tempfile.TemporaryDirectory() as root:
            ops = self.child_ops(root)
            with patch.object(c, 'verify', side_effect=ValueError('packet/source drift')), \
                    patch.object(c.subprocess, 'Popen') as launch, self.assertRaises(ValueError):
                ops.child('bootstrap', '/dev/cu.usbmodemFAKE01')
            launch.assert_not_called()
            self.assertEqual(list(ops.cycle.iterdir()), [])

    def test_uncertain_child_status_never_kills_supervisor_or_claims_completion(self):
        good = dict(status='BOOTSTRAP_WORKER_EXITED_NO_RUNTIME_VERDICT', worker_returncode=1)
        cases = [('missing', 1, None), ('wrong-cli-exit', 0, good),
                 ('unknown-status', 1, dict(good, status='HOST_ONLY_FAKE_SUCCESS')),
                 ('live-worker', 1, dict(good, worker_returncode=None)),
                 ('boolean-worker-code', 1, dict(good, worker_returncode=True)),
                 ('outer-timeout', c.subprocess.TimeoutExpired('fake', 150), None),
                 ('operator-interrupt', KeyboardInterrupt(), None)]
        for name, outcome, receipt in cases:
            with self.subTest(case=name), tempfile.TemporaryDirectory() as root:
                ops = self.child_ops(root)
                process = Mock(pid=4321)
                def wait(*, timeout):
                    if isinstance(outcome, BaseException):
                        raise outcome
                    if receipt is not None:
                        directory = ops.cycle/'bootstrap'
                        directory.mkdir()
                        (directory/'host-result.json').write_text(json.dumps(receipt))
                    return outcome
                process.wait.side_effect = wait
                with patch.object(c, 'verify'), patch.object(c.subprocess, 'Popen', return_value=process), \
                        self.assertRaises(c.base.WorkerUnknown):
                    ops.child('bootstrap', '/dev/cu.usbmodemFAKE01')
                process.kill.assert_not_called()
                process.terminate.assert_not_called()
                self.assertFalse((ops.cycle/'bootstrap-completed.json').exists())

    def test_bootstrap_outer_timeout_preserves_latch_and_forbids_reset(self):
        class TimeoutOps(Ops):
            def child(self, phase, port):
                self.lock_fd = 123
                child_args = self.args
                self.args = SimpleNamespace(manifest=Path('/host-only/manifest'),
                    host_source=Path('/host-only/source'), inventory=Path('/host-only/inventory'),
                    window_image=Path('/host-only/window'))
                try:
                    return c.Ops.bootstrap_child(self, port)
                finally:
                    self.args = child_args
        with tempfile.TemporaryDirectory() as root:
            ops = TimeoutOps(root)
            process = Mock(pid=4321)
            process.wait.side_effect = c.subprocess.TimeoutExpired('fake', 150)
            with patch.object(c, 'verify'), patch.object(c.subprocess, 'Popen', return_value=process):
                result = c.run_attempt(ops, {}, CANDIDATE)
            self.assertEqual(result['status'], 'WORKER_STATE_UNKNOWN_NO_RESET')
            self.assertTrue(ops.armed)
            self.assertEqual(ops.resets, 0)
            process.kill.assert_not_called()
            process.terminate.assert_not_called()
            recorded = json.loads((ops.cycle/'verified-inputs.json').read_text())
            self.assertEqual(recorded['continuing_source_sha256'], c.CONTINUING_SOURCES)
            self.assertEqual(recorded['virtual_input_source_sha256'], c.INPUT_HELPER_SHA)
            self.assertEqual(recorded['terminal_input_source_sha256'], c.TERMINAL_HELPER_SHA)
            for helper in (c.INPUT_HELPER, c.TERMINAL_HELPER):
                self.assertLess(ops.events.index('source-'+helper.replace('/', '__')), ops.events.index('reboot'))
            for source in (c.PROGRESS_BOOTSTRAP, c.BOOTARGS_HELPER, c.INPUT_HELPER, c.TERMINAL_HELPER):
                self.assertEqual((ops.cycle/('source-'+source.replace('/', '__'))).read_bytes(),
                                 (c.LAB/source).read_bytes())

    def test_terminal_source_snapshot_failure_aborts_before_arm_or_reboot(self):
        name = 'source-' + c.TERMINAL_HELPER.replace('/', '__')
        with tempfile.TemporaryDirectory() as root:
            ops = Ops(root, fail=name)
            result = c.run_attempt(ops, {}, CANDIDATE)
            self.assertEqual(result['status'], 'ABORTED_BEFORE_REBOOT')
            self.assertFalse(ops.armed)
            self.assertEqual(ops.resets, 0)
            self.assertNotIn('arm', ops.events)
            self.assertNotIn('reboot', ops.events)
            self.assertNotIn('bootstrap', ops.events)

    def test_native_nonzero_still_observes_linux_without_second_reset(self):
        class RebootedWithRoutingError(Ops):
            def reset(self):
                super().reset()
                raise ValueError('native rc255 after reboot ack; serial VDM failed')
        with tempfile.TemporaryDirectory() as root:
            ops = RebootedWithRoutingError(root)
            result = c.run_attempt(ops, {}, CANDIDATE)
            self.assertTrue(result['linux_verified'])
            self.assertEqual(ops.resets, 1)
            self.assertEqual(result['reset_attempts'], 1)
            self.assertIn('rc255', result['native_reset_error'])
            self.assertFalse(ops.armed)

    def test_native_nonzero_or_timeout_without_new_boot_keeps_latch(self):
        for exception in (ValueError('rc255'), c.base.NativeResetTimeout('native deadline')):
            class NoNewBoot(Ops):
                def reset(self):
                    self.resets += 1
                    self.phase = 'same-linux'
                    raise exception
            with self.subTest(exception=exception), tempfile.TemporaryDirectory() as root:
                ops = NoNewBoot(root)
                result = c.run_attempt(ops, {}, CANDIDATE)
                self.assertFalse(result['linux_verified'])
                self.assertTrue(ops.armed)
                self.assertEqual(ops.resets, 1)
                self.assertEqual(result['status'], 'RECOVERY_INCOMPLETE_NO_RETRY')

    def test_offline_default_does_not_import_launcher_or_open_target(self):
        with patch.object(c, 'Ops', side_effect=AssertionError('target')), \
                patch.object(c, 'verify', side_effect=AssertionError('verification')), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(c.main([]), 0)
        self.assertFalse(json.loads(output.getvalue())['device_contacted'])

    def test_unknown_guest_worker_leaves_latch_and_never_resets(self):
        with tempfile.TemporaryDirectory() as root:
            ops = Ops(root, fail='guest-unknown')
            result = c.run_attempt(ops, {}, CANDIDATE)
            self.assertEqual(result['status'], 'WORKER_STATE_UNKNOWN_NO_RESET')
            self.assertTrue(ops.armed)
            self.assertEqual(ops.resets, 0)

    def test_no_cli_is_a_failure_even_after_verified_linux_recovery(self):
        with tempfile.TemporaryDirectory() as root:
            ops = Ops(root)
            result = c.run_attempt(ops, {}, CANDIDATE)
            self.assertFalse(result['command_line_observed'])
            self.assertTrue(result['linux_verified'])
            self.assertEqual(result['reset_attempts'], 1)
            self.assertFalse(ops.armed)
            self.assertEqual(result['status'], 'NO_COMMAND_LINE_OBSERVED_LINUX_VERIFIED')

    def test_worker_timeout_remains_visible_with_raw_cli_and_verified_linux(self):
        with tempfile.TemporaryDirectory() as root:
            ops = Ops(root, guest_host_status='HOST_WORKER_TIMEOUT',
                      guest_worker_returncode=-9, diagnostics_complete=False)
            with patch.object(c, 'console_observation', return_value=True) as proof:
                result = c.run_attempt(ops, {}, CANDIDATE)
            proof.assert_called_once_with(ops.cycle/'guest', CANDIDATE)
            self.assertTrue(result['command_line_observed'])
            self.assertTrue(result['linux_verified'])
            self.assertEqual(result['status'], 'DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED')
            self.assertEqual(result['guest_host_status'], 'HOST_WORKER_TIMEOUT')
            self.assertEqual(result['guest_worker_returncode'], -9)
            self.assertIs(result['diagnostics_complete'], False)
            self.assertEqual(ops.resets, 1)
            self.assertEqual(result['reset_attempts'], 1)
            self.assertFalse(ops.armed)
            self.assertEqual(json.loads((ops.cycle/'result.json').read_text()), result)
            self.assertEqual(json.loads((ops.cycle/'guest/host-result.json').read_text()),
                             dict(status='HOST_WORKER_TIMEOUT', worker_returncode=-9))

    def test_diagnostics_completion_requires_its_own_explicit_true_receipt(self):
        for value in (False, None, 1, 'true', True):
            with self.subTest(receipt=value), tempfile.TemporaryDirectory() as root:
                ops = Ops(root, diagnostics_complete=value)
                with patch.object(c, 'console_observation', return_value=True):
                    result = c.run_attempt(ops, {}, CANDIDATE)
                self.assertTrue(result['command_line_observed'])
                self.assertTrue(result['linux_verified'])
                self.assertIs(result['diagnostics_complete'], value is True)
        class MissingDiagnostics(Ops):
            def child(self, phase, port):
                directory = super().child(phase, port)
                if phase == 'guest':
                    (directory/'exception-context.json').unlink()
                return directory
        with tempfile.TemporaryDirectory() as root:
            ops = MissingDiagnostics(root)
            with patch.object(c, 'console_observation', return_value=True):
                result = c.run_attempt(ops, {}, CANDIDATE)
            self.assertTrue(result['command_line_observed'])
            self.assertTrue(result['linux_verified'])
            self.assertIs(result['diagnostics_complete'], False)

    def test_automatic_linux_return_does_not_trigger_pd_reset(self):
        with tempfile.TemporaryDirectory() as root:
            ops = Ops(root, fail='auto-return')
            result = c.run_attempt(ops, {}, CANDIDATE)
            self.assertTrue(result['linux_verified'])
            self.assertEqual(ops.resets, 0)

    def test_main_cannot_succeed_when_latch_clear_failed(self):
        fake = SimpleNamespace(__enter__=None)
        class Context:
            def __enter__(self):
                return fake
            def __exit__(self, *args):
                pass
        outcome = dict(status='RECOVERY_INCOMPLETE_NO_RETRY', command_line_observed=True, linux_verified=True)
        with patch.object(c, 'Ops', return_value=Context()), patch.object(c, 'verify', return_value=({}, {}, None)), \
                patch.object(c, 'run_attempt', return_value=outcome), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(c.main(['--execute', '--candidate', '/host-only-fake',
                                     '--confirm-recovery', c.base.CONFIRMATION]), 1)

    def test_kernel_console_boolean_and_echo_are_insufficient(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            nonce = '0123456789ab'*4
            a, b = nonce[:24], nonce[24:]
            command = ("PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; "
                       f"printf '\\nDL9_BEGIN_%s%s\\n' '{a}' '{b}'; "
                       "{ uname -s; uname -m; uname -r; } 2>/dev/null; "
                       f"printf 'DL9_END_%s%s\\n' '{a}' '{b}'\r").encode()
            spec = importlib.util.spec_from_file_location('test_console_launcher', c.LAB/c.LAUNCHER)
            launcher = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(launcher)
            self.assertEqual(command, launcher.ConsoleProof(nonce).command())
            response = b'\r\nDL9_BEGIN_'+nonce.encode()+b'\r\nDarwin\r\narm64\r\n25.5.0\r\nDL9_END_'+nonce.encode()+b'\r\n'
            (root/'challenge-input.bin').write_bytes(command)
            (root/'before-start.json').write_text(json.dumps(dict(image_sha256=CANDIDATE['artifacts']['kernel'])))
            (root/'original-observations.json').write_text(json.dumps(dict(packet=CANDIDATE)))
            core_path = root/'capture-core.json'
            core = dict(status='CONTINUING_XNU_CORE_CAPTURED', console_capture_finalized=True,
                        console_close_error=None, diagnostics_complete=False)
            def put(raw, **kwargs):
                (root/'console.bin').write_bytes(raw)
                record = dict(nonce=nonce, command_line_observed=True, challenge_sent=True,
                              error=None, console_sha256=c.base.sha(raw), console_bytes=len(raw))
                record.update(kwargs)
                (root/'console-result.json').write_text(json.dumps(record))
            # Correct raw proof cannot substitute for durable console-close
            # certification. Diagnostics completion is a separate observation.
            put(command+response)
            self.assertFalse(c.console_observation(root, CANDIDATE))  # Missing core.
            for bad in ({}, dict(core, console_capture_finalized=False),
                        dict(core, console_capture_finalized=None),
                        dict(core, console_capture_finalized=1),
                        dict(core, console_capture_finalized='true'),
                        dict(core, console_close_error='OSError: synthetic close failure'),
                        dict(core, console_close_error=''),
                        dict(core, console_close_error=False)):
                with self.subTest(core=bad):
                    core_path.write_text(json.dumps(bad))
                    self.assertFalse(c.console_observation(root, CANDIDATE))
            core_path.write_text(json.dumps(core))
            self.assertTrue(c.console_observation(root, CANDIDATE))
            put(command)
            self.assertFalse(c.console_observation(root, CANDIDATE))
            put(command+response)
            self.assertTrue(c.console_observation(root, CANDIDATE))
            put(command+response.replace(b'\r', b'\r\r'))
            self.assertTrue(c.console_observation(root, CANDIDATE))
            put(command+response, console_sha256='0'*64)
            self.assertFalse(c.console_observation(root, CANDIDATE))
            put(command+response.replace(b'Darwin', b'Linux'))
            self.assertFalse(c.console_observation(root, CANDIDATE))
            put(command+response.replace(b'\r\narm64',
                b'\r\nuname(1) malloc: diagnostic must not be filtered\r\narm64'))
            self.assertFalse(c.console_observation(root, CANDIDATE))
            put(command+response)
            self.assertFalse(c.console_observation(root, {'artifacts': {'kernel': '0'*64}}))


if __name__ == '__main__':
    unittest.main()
