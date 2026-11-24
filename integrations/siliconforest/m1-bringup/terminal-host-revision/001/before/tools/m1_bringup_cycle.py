#!/usr/bin/env python3
"""One bounded continuing-XNU attempt and known-Linux recovery on the M1.

Uses the qualified 008 acquisition/recovery primitives without changing their
fixed diagnostic workload. A separate candidate and hash-bound 009 launcher are
required. This tool has no boot-storage, DFU, flash, or Maclab operations.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

LAB = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LAB))
from tools import m1_kernel_cycle as base

LAUNCHER = 'integrations/puredarwin/m1-bringup/host_progress.py'
INSPECTOR = 'integrations/puredarwin/m1-bringup/normal_image.py'
PROGRESS_BOOTSTRAP = 'integrations/puredarwin/m1-bringup/bootstrap_progress.py'
BOOTARGS_HELPER = 'integrations/puredarwin/m1-bringup/bootargs_progress.py'
INPUT_HELPER = 'integrations/puredarwin/m1-bringup/virtual_input_progress.py'
INPUT_HELPER_SHA = '689a8fb0fd7728d655d3baefbee18308cb2bce7333f1c0f4a76f37bc8e5673e1'
CONTINUING_SOURCES = {'integrations/puredarwin/m1-bringup/bootstrap_progress.py': '52fdd9045d0f2d14d08b38577cd3bfca512b9eb6e083afc82fa7c081350c56d8', 'integrations/puredarwin/m1-bringup/bootargs_progress.py': '8b131bd35dcfcd4097676c4d61f05c69b7115e609cdf07f49774a2f7846bb489'}
BASE_SHA = '3d274d1b699c0adfa1e38361d8348c3ee7567b854c5361ea80cbe23a18b7e3e6'


def verify(args):
    base.require(base.sha(base.read_file(LAB/'tools/m1_kernel_cycle.py')) == BASE_SHA,
                 'Frozen recovery/acquisition controller changed')
    for name, expected in ((LAUNCHER, args.launcher_sha256), (INSPECTOR, args.inspector_sha256)):
        base.require(isinstance(expected, str) and re.fullmatch('[0-9a-f]{64}', expected) and
                     base.sha(base.read_file(LAB/name)) == expected, '009 helper hash mismatch: ' + name)
    for name, expected in CONTINUING_SOURCES.items():
        base.require(base.sha(base.read_file(LAB/name)) == expected, 'Continuing bootstrap source changed: '+name)
    base.require(set(CONTINUING_SOURCES) == {PROGRESS_BOOTSTRAP, BOOTARGS_HELPER},
                 'Continuing bootstrap sources must be bound')
    base.require(base.sha(base.read_file(LAB/INPUT_HELPER)) == INPUT_HELPER_SHA,
                 'Virtual-input scheduler source changed')
    bootstrap = base.verify_local(args)
    # Verify the continuing copy too, including its frozen trampoline path,
    # before the target is rebooted or any serial device is opened.
    spec = importlib.util.spec_from_file_location('bound_continuing_bootstrap', LAB/PROGRESS_BOOTSTRAP)
    continuing = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = continuing
    spec.loader.exec_module(continuing)
    continuing.verify(args)
    spec = importlib.util.spec_from_file_location('bound_continuing_host', LAB/LAUNCHER)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    values = argparse.Namespace(manifest=args.candidate, bootstrap_manifest=args.manifest,
                                host_source=args.host_source, inventory=args.inventory)
    candidate = module.verify_inputs(values)
    return bootstrap, candidate.report, module


class Ops(base.RealOps):
    def bootstrap_child(self, port):
        phase = 'bootstrap'
        source, seconds = PROGRESS_BOOTSTRAP, 120
        verify(self.args)  # Recheck before every opening, including packet/source drift.
        directory = self.cycle / phase  # Child insists this does not exist yet.
        argv = [sys.executable, '-I', str(LAB / source), '--manifest', str(self.args.manifest),
                '--host-source', str(self.args.host_source), '--inventory', str(self.args.inventory),
                '--execute', '--device', port, '--private-directory', str(directory),
                '--confirm-recovery', base.CONFIRMATION, '--timeout', str(seconds)]
        if phase == 'bootstrap':
            argv += ['--window-image', str(self.args.window_image)]
        with (self.cycle / (phase + '-supervisor.log')).open('xb') as log:
            child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                     start_new_session=True, pass_fds=(self.lock_fd,))
            self.save(phase + '-pid.json', {'pid': child.pid, 'worker_timeout_seconds': seconds})
            try:
                code = child.wait(timeout=seconds + 30)
                result = base.read_json(directory / 'host-result.json')
                statuses = ({'BOOTSTRAP_WORKER_EXITED_NO_RUNTIME_VERDICT', 'BOOTSTRAP_WORKER_TIMEOUT',
                             'BOOTSTRAP_OPERATOR_INTERRUPTED'} if phase == 'bootstrap' else
                            {'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT', 'HOST_WORKER_TIMEOUT', 'HOST_OPERATOR_INTERRUPTED'})
                base.require(code == 1 and result.get('status') in statuses and
                        type(result.get('worker_returncode')) is int, 'Missing terminated worker status')
                self.save(phase + '-completed.json', {'cli_returncode': code, 'host_result': result})
            except BaseException as error:
                # Do not kill the supervisor and orphan its independently sessioned worker.
                raise base.WorkerUnknown('Child CLI/worker completion unknown; no reset: ' + phase) from error
        return directory


    def child(self, phase, port):
        if phase == 'bootstrap':
            return self.bootstrap_child(port)
        _, _, module = verify(self.args)
        topology = self.command('console-registry', ['/usr/sbin/ioreg', '-a', '-r', '-l', '-c', 'IOUSBHostDevice'], 5)
        base.require(topology.returncode == 0, 'USB topology query failed')
        pair = module.cdc_pair(topology.stdout, port)
        self.save('console-pair.json', pair)
        directory = self.cycle / 'guest'
        argv = [sys.executable, '-I', str(LAB/LAUNCHER), '--manifest', str(self.args.candidate),
                '--bootstrap-manifest', str(self.args.manifest), '--host-source', str(self.args.host_source),
                '--inventory', str(self.args.inventory), '--execute', '--device', port,
                '--console-device', pair['console'], '--confirm-recovery', base.CONFIRMATION,
                '--private-directory', str(directory), '--runtime', str(self.args.runtime), '--timeout', '240']
        with (self.cycle/'guest-supervisor.log').open('xb') as log:
            child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                     start_new_session=True, pass_fds=(self.lock_fd,))
            self.save('guest-pid.json', dict(pid=child.pid, worker_timeout_seconds=240))
            try:
                code = child.wait(timeout=270)
                result = base.read_json(directory/'host-result.json')
                base.require(code == 1 and result.get('status') in {
                    'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT', 'HOST_WORKER_TIMEOUT', 'HOST_OPERATOR_INTERRUPTED'} and
                    type(result.get('worker_returncode')) is int, 'Guest worker termination unverified')
                self.save('guest-completed.json', dict(cli_returncode=code, host_result=result))
            except BaseException as error:
                raise base.WorkerUnknown('Continuing guest worker state unknown; no reset') from error
        return directory


def console_observation(directory, candidate):
    """Recompute the response from raw VUART bytes; JSON booleans are insufficient."""
    try:
        result = base.read_json(directory/'console-result.json')
        core = base.read_json(directory/'capture-core.json')
        base.require(core.get('console_capture_finalized') is True and
                     core.get('console_close_error') is None,
                     'Console finalization was not certified before diagnostics')
        before = base.read_json(directory/'before-start.json')
        observed = base.read_json(directory/'original-observations.json')
        raw = base.read_file(directory/'console.bin', 32 << 20)
        challenge = base.read_file(directory/'challenge-input.bin', 4096)
        nonce = result['nonce']
        base.require(isinstance(nonce, str) and re.fullmatch('[0-9a-f]{48}', nonce), 'Invalid challenge receipt')
        base.require(result.get('command_line_observed') is True and result.get('error') is None and
                     result.get('challenge_sent') is True and result.get('console_sha256') == base.sha(raw) and
                     result.get('console_bytes') == len(raw), 'Console receipt mismatch')
        base.require(before['image_sha256'] == candidate['artifacts']['kernel'] and
                     observed['packet']['artifacts'] == candidate['artifacts'], 'Wrong launch artifact')
        # Echo alone has split nonce literals and can never contain these complete
        # markers. The exact input is checked independently against the nonce.
        a, b = nonce[:24], nonce[24:]
        expected = ("PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; "
                    f"printf '\\nDL9_BEGIN_%s%s\\n' '{a}' '{b}'; "
                    "{ uname -s; uname -m; uname -r; } 2>/dev/null; "
                    f"printf 'DL9_END_%s%s\\n' '{a}' '{b}'\r").encode()
        base.require(challenge == expected and nonce.encode() not in challenge, 'Unexpected challenge bytes')
        pattern = (b'\nDL9_BEGIN_' + nonce.encode() +
                   rb'\nDarwin\narm64\n[0-9][0-9A-Za-z_.+-]{0,100}\nDL9_END_' + nonce.encode() + b'\n')
        return re.search(pattern, raw.replace(b'\r', b'')) is not None
    except (OSError, ValueError, KeyError, TypeError):
        return False


def run_attempt(ops, bootstrap, candidate):
    ops.start_cycle(1)
    result = dict(status='NOT_STARTED', command_line_observed=False, linux_verified=False, reset_attempts=0)
    intended, workers_stopped, bootstrap_attempted = False, True, False
    try:
        ops.save('verified-inputs.json', dict(bootstrap=bootstrap, candidate=candidate,
                  controller_sha256=base.sha(base.read_file(Path(__file__))),
                  launcher_sha256=ops.args.launcher_sha256, inspector_sha256=ops.args.inspector_sha256,
                  continuing_source_sha256=CONTINUING_SOURCES, virtual_input_source_sha256=INPUT_HELPER_SHA))
        # Keep exact helper bytes per attempt as well as hashes. Later loader
        # revisions must not erase the source needed to review an older run.
        for name in sorted(set(base.SOURCE_SHA) | {LAUNCHER, INSPECTOR,
                                                  'tools/m1_kernel_cycle.py', 'tools/m1_bringup_cycle.py',
                                                  PROGRESS_BOOTSTRAP, BOOTARGS_HELPER, INPUT_HELPER}):
            ops.save('source-' + name.replace('/', '__'), base.read_file(LAB/name))
        baseline = base.check_linux(ops.observe())
        ops.save('baseline.json', baseline)
        ops.arm()
        ops.save('reboot-intent.json', dict(command='orderly reboot', baseline_boot_id=baseline['boot_id']))
        intended = True
        ops.reboot()
        initial = base.wait_cdc(ops, 90)
        ops.save('bootstrap-port.json', initial)
        bootstrap_attempted, workers_stopped = True, False
        directory = ops.child('bootstrap', initial['callout'])
        workers_stopped = True
        ack = base.read_json(directory/'vector-ack.json')
        base.require(ack.get('status') == 'VECTOR_ACKNOWLEDGED_RUNTIME_UNVERIFIED' and
                     ack.get('runtime_verified') is False, 'Missing bootstrap vector acknowledgement')
        selected = base.wait_cdc(ops, 25, previous=initial)
        ops.save('guest-port.json', selected)
        workers_stopped = False
        guest = ops.child('guest', selected['callout'])
        workers_stopped = True
        host_result = base.read_json(guest/'host-result.json')
        result['guest_host_status'] = host_result['status']
        result['guest_worker_returncode'] = host_result['worker_returncode']
        result['diagnostics_complete'] = False
        if (guest/'exception-context.json').exists():
            result['diagnostics_complete'] = base.read_json(
                guest/'exception-context.json').get('diagnostics_complete') is True
        result['command_line_observed'] = console_observation(guest, candidate)
        result['status'] = 'DARWIN_COMMAND_LINE_OBSERVED' if result['command_line_observed'] else 'NO_COMMAND_LINE_OBSERVED'
    except (Exception, KeyboardInterrupt) as error:
        result.update(status='EXPERIMENT_STOPPED', experiment_error=f'{type(error).__name__}: {error}')
    if not intended:
        result['status'] = 'ABORTED_BEFORE_REBOOT'
    elif not workers_stopped:
        result['status'] = 'WORKER_STATE_UNKNOWN_NO_RESET'
    else:
        try:
            if not bootstrap_attempted:
                returned = base.recover_linux(ops, baseline['boot_id'])
            else:
                try:
                    returned = ops.observe()
                except (ConnectionError, TimeoutError):
                    returned = None
                if returned is not None:
                    base.check_linux(returned, previous=baseline['boot_id'])
                else:
                    cdc = ops.discover()
                    ops.save('recovery-cdc.json', dict(primary_cdc=cdc, linux_probe_failed=True))
                    if cdc is not None:
                        ops.save('reset-intent.json', dict(attempt=1, workers_stopped=True,
                                  acquisition_probes_stopped=True, primary_cdc=cdc, args=['reboot', 'serial']))
                        result['reset_attempts'] = 1
                        try:
                            ops.reset()
                        except Exception as error:
                            # A completed reboot can be followed by a failed
                            # serial-routing VDM. Even a tool timeout permits
                            # passive SSH observation, never another reset.
                            result['native_reset_error'] = f'{type(error).__name__}: {error}'
                            ops.save('native-reset-error-passive-only.json', dict(
                                error=result['native_reset_error'], retry_permitted=False,
                                target_state_inferred_from_native_output=False))
                    returned = base.recover_linux(ops, baseline['boot_id'])
            ops.save('returned-linux.json', returned)
            result['linux_verified'] = True
            ops.clear()
            result['status'] += '_LINUX_VERIFIED'
        except (Exception, KeyboardInterrupt) as error:
            result.update(status='RECOVERY_INCOMPLETE_NO_RETRY', recovery_error=f'{type(error).__name__}: {error}')
    ops.save('result.json', result)
    return result


def main(argv=None):
    parser = base.parser()
    parser.description = __doc__
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--launcher-sha256')
    parser.add_argument('--inspector-sha256')
    parser.add_argument('--runtime', type=int, default=90)
    parser.set_defaults(private_root=base.WORK/'kernel-bringup-009')
    args = parser.parse_args(argv)
    if not args.execute:
        print(json.dumps(dict(status='OFFLINE_PLAN_ONLY', device_contacted=False, command_line_observed=False)))
        return 0
    try:
        base.require(args.cycles == 1 and args.candidate and 5 <= args.runtime <= 180,
                     'One concrete candidate/attempt required')
        base.require(args.confirm_recovery == base.CONFIRMATION, 'Recovery confirmation required')
        os.umask(0o077)
        with Ops(args) as ops:
            bootstrap, candidate, _ = verify(args)
            result = run_attempt(ops, bootstrap, candidate)
        print(json.dumps(result))
        return 0 if (result['status'] == 'DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED' and
                     result['command_line_observed'] and result['linux_verified']) else 1
    except Exception as error:
        print(json.dumps(dict(status='CONTROLLER_STOPPED_REQUIRES_REVIEW', error=str(error), command_line_observed=False)))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
