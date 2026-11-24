#!/usr/bin/env python3
"""One PCIe proxy session on the M1 and Linux recovery (M0/M1 of the PCIe/Wi-Fi plan).

Linux -> reboot -> boot window -> RAM m1n1 (existing bootstrap) -> run
integrations/puredarwin/m1-apcie-bridge/proxy_linkup.py against the proxy ->
reset back to Linux. Reuses the 008 acquisition and recovery code unchanged.
No hypervisor, XNU, boot-storage, DFU or flash operations.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

LAB = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LAB))
from tools import m1_kernel_cycle as base

BOOTSTRAP = 'integrations/puredarwin/m1-bringup/bootstrap_progress.py'
SCRIPT = 'integrations/puredarwin/m1-apcie-bridge/proxy_linkup.py'


class Ops(base.RealOps):
    def run_child(self, phase, argv, seconds):
        with (self.cycle / (phase + '-supervisor.log')).open('xb') as log:
            child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                     start_new_session=True, pass_fds=(self.lock_fd,))
            self.save(phase + '-pid.json', {'pid': child.pid, 'timeout_seconds': seconds})
            try:
                code = child.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=10)
                code = -9
            self.save(phase + '-completed.json', {'returncode': code})
        return code

    def bootstrap(self, port):
        directory = self.cycle / 'bootstrap'
        argv = [sys.executable, '-I', str(LAB / BOOTSTRAP), '--manifest', str(self.args.manifest),
                '--host-source', str(self.args.host_source), '--inventory', str(self.args.inventory),
                '--execute', '--device', port, '--private-directory', str(directory),
                '--confirm-recovery', base.CONFIRMATION, '--timeout', '120',
                '--window-image', str(self.args.window_image)]
        self.run_child('bootstrap', argv, 150)
        return directory

    def linkup(self, port):
        directory = self.cycle / 'pcie'
        argv = [sys.executable, str(LAB / SCRIPT), '--device', port,
                '--host-source', str(self.args.host_source), '--out', str(directory)]
        if self.args.no_smc:
            argv.append('--no-smc')
        os.environ['PCIE_CHIP_PROBE'] = '1'
        return self.run_child('pcie', argv, self.args.runtime), directory


def run(ops):
    ops.start_cycle(1)
    result = dict(status='NOT_STARTED', linux_verified=False, reset_attempts=0)
    intended = bootstrap_attempted = False
    baseline = None
    try:
        baseline = base.check_linux(ops.observe())
        ops.save('baseline.json', baseline)
        ops.arm()
        intended = True
        ops.reboot()
        initial = base.wait_cdc(ops, 90)
        ops.save('bootstrap-port.json', initial)
        bootstrap_attempted = True
        directory = ops.bootstrap(initial['callout'])
        ack = base.read_json(directory / 'vector-ack.json')
        base.require(ack.get('status') == 'VECTOR_ACKNOWLEDGED_RUNTIME_UNVERIFIED', 'Bootstrap vector not acknowledged')
        selected = base.wait_cdc(ops, 25, previous=initial)
        ops.save('proxy-port.json', selected)
        code, out = ops.linkup(selected['callout'])
        result['pcie_returncode'] = code
        try:
            pcie = base.read_json(out / 'result.json')
            result['pcie'] = {k: pcie.get(k) for k in ('stage', 'ok', 'pcie_init', 'm0_port_ready', 'link_up')}
        except Exception as error:
            result['pcie'] = f'no result: {error}'
        result['status'] = 'PCIE_SESSION_FINISHED'
    except (Exception, KeyboardInterrupt) as error:
        result.update(status='EXPERIMENT_STOPPED', experiment_error=f'{type(error).__name__}: {error}')
    if not intended:
        result['status'] = 'ABORTED_BEFORE_REBOOT'
    else:
        try:
            returned = None
            if bootstrap_attempted:
                try:
                    returned = ops.observe()
                    base.check_linux(returned, previous=baseline['boot_id'])
                except (ConnectionError, TimeoutError):
                    returned = None
                if returned is None:
                    cdc = ops.discover()
                    ops.save('recovery-cdc.json', dict(primary_cdc=cdc))
                    if cdc is not None:
                        result['reset_attempts'] = 1
                        try:
                            ops.reset()
                        except Exception as error:
                            result['native_reset_error'] = f'{type(error).__name__}: {error}'
            if returned is None:
                returned = base.recover_linux(ops, baseline['boot_id'])
            ops.save('returned-linux.json', returned)
            result['linux_verified'] = True
            ops.clear()
            result['status'] += '_LINUX_VERIFIED'
        except (Exception, KeyboardInterrupt) as error:
            result.update(status='RECOVERY_INCOMPLETE', recovery_error=f'{type(error).__name__}: {error}')
    ops.save('result.json', result)
    result['run_directory'] = str(ops.cycle)
    return result


def main(argv=None):
    parser = base.parser()
    parser.description = __doc__
    parser.add_argument('--runtime', type=int, default=120)
    parser.add_argument('--no-smc', action='store_true')
    parser.set_defaults(private_root=base.WORK / 'pcie-cycles-011')
    args = parser.parse_args(argv)
    if not args.execute:
        print(json.dumps(dict(status='OFFLINE_PLAN_ONLY', device_contacted=False)))
        return 0
    base.require(args.confirm_recovery == base.CONFIRMATION, 'Recovery confirmation required')
    os.umask(0o077)
    with Ops(args) as ops:
        result = run(ops)
    print(json.dumps(result))
    return 0 if result.get('linux_verified') else 1


if __name__ == '__main__':
    raise SystemExit(main())
