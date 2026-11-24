#!/usr/bin/env python3
"""Fixed J293/M5 bounded entry/park/recovery cycles; offline unless --execute."""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shlex
import stat
import subprocess
import sys
import tempfile
import time
import uuid

LAB = Path(__file__).resolve().parents[1]
WORK = Path('/Users/chriskearney/darwin-lab-firstboot-007')
LOCK = WORK / 'm1-controller-008.lock'  # Same inode/path as the recovery controller.
INCOMPLETE = WORK / 'm1-kernel-cycle-incomplete.json'
KEYS = WORK / 'controller-ssh-008'
NATIVE = WORK / 'native-control-src/macvdmtool'
NATIVE_SHA = 'db6da3ff2f86d9663f748d975ce1ce988c82047014912018775b61f207b6ff4a'
HOST = '10.1.1.127'
LINUX = '7.2.11-6-1-ARCH'
LOADER_SHA = 'e4ead838bb87a04dd00909edf8a598f813056ee2f9611777a4a77c4bf7351e27'
KERNEL_SHA = 'd6c7ba75025d595d12e171810b89344dfaa6b467f7a444bcce277a35b8175ff2'
BOOTSTRAP = 'integrations/m1-boot-window/bootstrap.py'
GUEST = 'integrations/puredarwin/m1-firstboot/host_console.py'
SOURCE_SHA = {
    BOOTSTRAP: '350cd127fe7b41586c562847dd1b8c1cd6054df42276019844dfa2012e3beafb',
    GUEST: 'a9f68695c6465bc5a3c1a9df04f67da6a72f99265e899cf3b89ba671ab6d4417',
    'tools/__init__.py': '11bba8a654b073bcc3eed4258f0b29d1f9dce1623560ef3e45aee3192c07249e',
    'tools/lab.py': 'fefcc5d960b753d7751dce6a0fd96d4ff1e9a7baacc1a97ba9906c05136e408f',
    'tools/m1_firstboot.py': 'd0e739b90b98553b4b662ae426c063416e8610b5080bdf5cf706ea70e4a2de93',
    'tools/m1_proxy_preflight.py': '37be2d4abdec4ed6c18158cdc5840924a9387d4ec576769b911e3fda213bb00e',
}
CHECKPOINT = (b'HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm '
              b'next=park kernel_init=not_started')
CONFIRMATION = 'J293_RECOVERY_READY'
UNITS = ('lab-agent.service', 'maclab-lab-boot-session.service',
         'maclab-lab-boot-lock.service', 'maclab-lab-boot-deadlines.service')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_file(path, maximum=64 << 20):
    path = Path(path)
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_size <= maximum, 'Not a bounded regular file: ' + str(path))
    return path.read_bytes()


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result
    return json.loads(read_file(path, 4 << 20), object_pairs_hook=unique)


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def durable(path, data):
    """Exclusive private record, including file and directory fsync before actions."""
    raw = data if isinstance(data, bytes) else (json.dumps(data, indent=2) + '\n').encode()
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'wb') as out:
        out.write(raw)
        out.flush()
        os.fsync(out.fileno())
    sync_directory(Path(path).parent)


def verify_local(args):
    for name, expected in SOURCE_SHA.items():
        require(sha(read_file(LAB / name)) == expected, 'Helper source changed: ' + name)
    spec = importlib.util.spec_from_file_location('cycle_verified_bootstrap', LAB / BOOTSTRAP)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    inputs, _, _ = module.verify(args)  # Packet bytes, entry, receipts and pinned host source; no device.
    require(sha(inputs.contents['kernel']) == KERNEL_SHA, 'Unexpected kernel candidate')
    return inputs.report


def primary_cdc(raw):
    """Use full IOService descendant properties; interface 1 is primary data, 3 is VUART."""
    devices = plistlib.loads(raw) if raw.strip() else []  # Empty ioreg stdout is normal.
    require(isinstance(devices, list), 'Invalid IORegistry roots')
    candidates = []
    def walk(node, identity, interfaces=()):
        require(isinstance(node, dict), 'Invalid IORegistry node')
        if 'bInterfaceNumber' in node:
            number = node['bInterfaceNumber']
            require(type(number) is int, 'Invalid USB interface number')
            interfaces += (number,)
        if 'IOCalloutDevice' in node and 1 in interfaces and not {2, 3}.intersection(interfaces):
            port = node['IOCalloutDevice']
            require(isinstance(port, str) and re.fullmatch(r'/dev/cu\.usbmodem[A-Za-z0-9_.-]+', port),
                    'Invalid primary CDC callout')
            require(all(type(value) is int and 0 < value < 1 << 64 for value in identity.values()),
                    'Missing or invalid USB registry/session identity')
            candidates.append({'callout': port, **identity})
        for child in node.get('IORegistryEntryChildren', []):
            walk(child, identity, interfaces)
    for device in devices:
        require(isinstance(device, dict), 'Invalid USB root')
        if (device.get('idVendor'), device.get('idProduct'), device.get('locationID')) == (0x1209, 0x316d, 0x100000):
            walk(device, {key: device.get(key) for key in ('IORegistryEntryID', 'sessionID')})
    require(len(candidates) <= 1, 'Ambiguous primary CDC topology')
    return candidates[0] if candidates else None


def check_linux(value, previous=None):
    require(isinstance(value, dict), 'Missing Linux observation')
    require(value.get('kernel') == LINUX and value.get('loader_sha256') == LOADER_SHA,
            'Linux kernel or installed candidate changed')
    require({'apple,j293', 'apple,t8103'} <= set(value.get('compatible', [])), 'Unexpected Linux target')
    boot = value.get('boot_id')
    require(isinstance(boot, str) and str(uuid.UUID(boot)) == boot, 'Invalid Linux boot UUID')
    require(previous is None or boot != previous, 'Linux boot UUID did not change')
    for mount in ('/', '/boot', '/boot/efi'):
        item = value.get('mounts', {}).get(mount, {})
        require('rw' in item.get('options', []) and 'ro' not in item.get('options', []), 'Mount is not writable: ' + mount)
    require(value['mounts']['/boot/efi']['type'] == 'vfat', 'Unexpected ESP filesystem')
    watchdogs = value.get('watchdogs', {})
    for key, expected in (('RuntimeWatchdogUSec', 30_000_000), ('RebootWatchdogUSec', 120_000_000)):
        match = re.fullmatch(r'(\d+)(us|ms|s|min)?', str(watchdogs.get(key, '')))
        require(match is not None, 'Unknown watchdog observation: ' + key)
        scale = {None: 1, 'us': 1, 'ms': 1000, 's': 1_000_000, 'min': 60_000_000}[match[2]]
        require(int(match[1]) * scale == expected, 'Known Linux watchdog baseline changed: ' + key)
    require(value.get('units') == {unit: 'inactive' for unit in UNITS}, 'Existing lab service is active or unknown')
    return value


def entry_observation(directory, packet):
    """Conjunction of exact console line and pinned capture conditions; never a full boot verdict."""
    try:
        lines = read_file(directory / 'worker.log').splitlines(keepends=True)
        console = any(line.endswith(b'\n') and line.rstrip(b'\r\n') in
                      (CHECKPOINT, b'TTY> ' + CHECKPOINT) for line in lines)
        ctx = read_json(directory / 'exception-context.json')
        raw = read_file(directory / 'exception-context.bin', 0x10000)
        before = read_json(directory / 'before-start.json')
        original = read_json(directory / 'original-observations.json')
        layout = before['layout']
        require(original['layout'] == layout and original['packet']['artifacts'] == packet['artifacts'],
                'Context packet/layout binding differs')
        cpus = [cpu['cpu_id'] for cpu in original['snapshot']['cpus'] if cpu['state'] == 'running']
        require(len(cpus) == 1, 'Ambiguous observed CPU')
        expected = {'elr': layout['guest_base'] + packet['kernel']['park_address'] - layout['vmin'],
                    'x0': layout['guest_base'] + layout['bootargs_off'], 'x19': 3, 'spsr': 0x3c5, 'cpu_id': cpus[0]}
        observed = ctx['observed']
        matches = all(type(observed.get(k)) is int and
                      (observed[k] & 0x3df if k == 'spsr' else observed[k]) == v for k, v in expected.items())
        context = (ctx.get('expected_park_conditions_match') is True and ctx.get('expected') == expected
                   and ctx.get('interrupt_write_completed') is True and not ctx.get('context_error')
                   and ctx.get('event', {}).get('reason') == 3 and ctx.get('event', {}).get('code') == 3
                   and ctx.get('raw_context_sha256') == sha(raw) and matches)
        return console and context
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return False


class WorkerUnknown(RuntimeError):
    """Never reset after this: the separately sessioned device worker may remain alive."""


class NativeResetTimeout(TimeoutError):
    """Host killed macvdmtool at its deadline; ACE mode and target reset state are unknown."""


class RealOps:
    def __init__(self, args):
        self.args, self.lock_fd, self.sequence = args, None, 0
        self.clock, self.sleep = time.monotonic, time.sleep

    def __enter__(self):
        require(platform.system() == 'Darwin' and platform.machine() == 'arm64' and os.geteuid() == 0,
                'Execute requires root on the M5 Darwin arm64 host')
        self.lock_fd = os.open(LOCK, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        try:
            require(stat.S_ISREG(os.fstat(self.lock_fd).st_mode), 'Invalid target lock')
            fcntl.flock(self.lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            require(not INCOMPLETE.exists() and not INCOMPLETE.is_symlink(), 'Prior incomplete cycle requires independent review')
            require(sha(read_file(NATIVE)) == NATIVE_SHA, 'Native reset binary changed')
            for name in ('id_ed25519', 'known_hosts'):
                info = (KEYS / name).lstat()  # Never read private key bytes.
                require(stat.S_ISREG(info.st_mode) and info.st_size > 0 and not info.st_mode & 0o022,
                        'Unsafe dedicated SSH file')
            self.known_hosts_sha = sha(read_file(KEYS / 'known_hosts'))
            parent = self.args.private_root.absolute()
            require(parent.resolve() == parent and not parent.is_relative_to(LAB) and
                    not parent.is_relative_to(KEYS), 'Private root must be outside repository/key directory without symlinks')
            parent.mkdir(mode=0o700, exist_ok=True)
            require(parent.stat().st_uid == os.geteuid() and not parent.stat().st_mode & 0o077,
                    'Private root must be root-owned mode 0700')
            self.directory = Path(tempfile.mkdtemp(prefix='cycle-', dir=parent))
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *unused):
        # Close only: an uncertain child CLI inherits this flock until it exits.
        if self.lock_fd is not None:
            os.close(self.lock_fd)
            self.lock_fd = None

    def start_cycle(self, number):
        self.cycle = self.directory / f'{number:03d}'
        self.cycle.mkdir(mode=0o700)
        self.sequence = 0
        return self.cycle

    def save(self, name, data):
        durable(self.cycle / name, data)

    def arm(self):
        durable(INCOMPLETE, {'run_directory': str(self.cycle), 'status': 'INCOMPLETE_UNTIL_WORKERS_STOPPED_AND_LINUX_VERIFIED'})

    def clear(self):
        INCOMPLETE.unlink()
        sync_directory(INCOMPLETE.parent)

    def command(self, label, argv, timeout):
        self.sequence += 1
        prefix = f'{self.sequence:03d}-{label}'
        try:
            result = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            self.save(prefix + '.json', {'timed_out': True})
            self.save(prefix + '.stdout', error.stdout or b'')
            self.save(prefix + '.stderr', error.stderr or b'')
            raise TimeoutError(label + ' timed out') from error
        self.save(prefix + '.stdout', result.stdout)
        self.save(prefix + '.stderr', result.stderr)
        self.save(prefix + '.json', {'returncode': result.returncode})
        return result

    def ssh(self, remote, timeout=10):
        require(sha(read_file(KEYS / 'known_hosts')) == self.known_hosts_sha, 'Pinned known_hosts changed')
        argv = ['/usr/bin/ssh', '-F', '/dev/null', '-T', '-S', 'none', '-o', 'BatchMode=yes',
                '-o', 'IdentitiesOnly=yes', '-o', 'IdentityAgent=none', '-o', 'StrictHostKeyChecking=yes',
                '-o', 'GlobalKnownHostsFile=/dev/null', '-o', 'UpdateHostKeys=no', '-o', 'ClearAllForwardings=yes',
                '-o', 'ConnectTimeout=3', '-o', 'ConnectionAttempts=1',
                '-o', 'UserKnownHostsFile=' + str(KEYS / 'known_hosts'), '-i', str(KEYS / 'id_ed25519'),
                'chrisk@' + HOST, remote]
        return self.command('ssh', argv, timeout)

    def observe(self):
        script = ("import hashlib,json,os,pathlib,subprocess\n"
                  "p=pathlib.Path\n"
                  "mounts={}\n"
                  "for row in p('/proc/mounts').read_text().splitlines():\n"
                  " f=row.split()\n"
                  " if f[1] in ('/','/boot','/boot/efi'): mounts[f[1]]={'type':f[2],'options':f[3].split(',')}\n"
                  "units=" + repr(UNITS) + "\n"
                  "wdt=subprocess.run(['systemctl','show','-p','RuntimeWatchdogUSec','-p','RebootWatchdogUSec'],capture_output=True,text=True,timeout=2,check=True)\n"
                  "print(json.dumps({'boot_id':p('/proc/sys/kernel/random/boot_id').read_text().strip(),"
                  "'kernel':os.uname().release,'compatible':p('/sys/firmware/devicetree/base/compatible').read_bytes().decode().split('\\0'),"
                  "'loader_sha256':hashlib.sha256(p('/boot/efi/m1n1/boot.bin').read_bytes()).hexdigest(),'mounts':mounts,"
                  "'watchdogs':dict(line.split('=',1) for line in wdt.stdout.splitlines()),"
                  "'units':{u:subprocess.run(['systemctl','is-active',u],capture_output=True,text=True,timeout=2).stdout.strip() for u in units}}))")
        result = self.ssh('sudo -n python3 -c ' + shlex.quote(script))
        if result.returncode:
            raise ConnectionError('Known-host Linux SSH unavailable')
        return json.loads(result.stdout)

    def reboot(self):
        result = self.ssh('sudo -n systemctl reboot', 20)
        require(result.returncode in (0, 255), 'Orderly reboot request refused')

    def discover(self):
        result = self.command('ioreg', ['/usr/sbin/ioreg', '-a', '-r', '-l', '-c', 'IOUSBHostDevice'], 3)
        require(result.returncode == 0, 'IORegistry query failed')
        return primary_cdc(result.stdout)

    def child(self, phase, port):
        source, seconds = (BOOTSTRAP, 120) if phase == 'bootstrap' else (GUEST, 60)
        verify_local(self.args)  # Recheck before every opening, including packet/source drift.
        directory = self.cycle / phase  # Child insists this does not exist yet.
        argv = [sys.executable, '-I', str(LAB / source), '--manifest', str(self.args.manifest),
                '--host-source', str(self.args.host_source), '--inventory', str(self.args.inventory),
                '--execute', '--device', port, '--private-directory', str(directory),
                '--confirm-recovery', CONFIRMATION, '--timeout', str(seconds)]
        if phase == 'bootstrap':
            argv += ['--window-image', str(self.args.window_image)]
        with (self.cycle / (phase + '-supervisor.log')).open('xb') as log:
            child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                     start_new_session=True, pass_fds=(self.lock_fd,))
            self.save(phase + '-pid.json', {'pid': child.pid, 'worker_timeout_seconds': seconds})
            try:
                code = child.wait(timeout=seconds + 30)
                result = read_json(directory / 'host-result.json')
                statuses = ({'BOOTSTRAP_WORKER_EXITED_NO_RUNTIME_VERDICT', 'BOOTSTRAP_WORKER_TIMEOUT',
                             'BOOTSTRAP_OPERATOR_INTERRUPTED'} if phase == 'bootstrap' else
                            {'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT', 'HOST_WORKER_TIMEOUT', 'HOST_OPERATOR_INTERRUPTED'})
                require(code == 1 and result.get('status') in statuses and
                        type(result.get('worker_returncode')) is int, 'Missing terminated worker status')
                self.save(phase + '-completed.json', {'cli_returncode': code, 'host_result': result})
            except BaseException as error:
                # Do not kill the supervisor and orphan its independently sessioned worker.
                raise WorkerUnknown('Child CLI/worker completion unknown; no reset: ' + phase) from error
        return directory

    def reset(self):
        require(sha(read_file(NATIVE)) == NATIVE_SHA, 'Native reset binary changed')
        try:
            result = self.command('native-reset', [str(NATIVE), 'reboot', 'serial'], 12)
        except TimeoutError as error:
            self.save('native-reset-timeout.json', {'timeout_seconds': 12, 'native_tool_killed': True,
                      'ace_state': 'unknown', 'target_reset_state': 'unknown', 'retry_permitted': False})
            raise NativeResetTimeout('macvdmtool killed at 12-second deadline; ACE state unknown; no retry') from error
        require(result.returncode == 0, 'Single native reset failed; no retry')

    def refresh_neighbor(self):
        self.command('neighbor-refresh', ['/usr/sbin/arp', '-d', HOST], 5)


def wait_cdc(ops, seconds, previous=None):
    end = ops.clock() + seconds
    while ops.clock() < end:
        selected = ops.discover()
        if selected and (previous is None or
                         (selected['IORegistryEntryID'], selected['sessionID']) !=
                         (previous['IORegistryEntryID'], previous['sessionID'])):
            return selected
        ops.sleep(.15)
    raise TimeoutError('No unique primary CDC within discovery deadline')


def recover_linux(ops, old_boot):
    start, refreshed = ops.clock(), False
    while ops.clock() - start < 150:
        try:
            value = ops.observe()
        except (ConnectionError, TimeoutError):
            value = None
        if value is not None:
            check_linux(value)  # Wrong target/image/mounts is a refusal, not a retryable connection failure.
            if value['boot_id'] != old_boot:
                return value
        if value is None and not refreshed and ops.clock() - start >= 25:
            refreshed = True
            ops.save('neighbor-refresh-intent.json', {'target_only': HOST, 'attempt': 1, 'cause_unproven': True})
            ops.refresh_neighbor()
        ops.sleep(3)
    raise TimeoutError('No new verified Linux boot within recovery deadline')


def run_cycle(ops, packet, number):
    directory = ops.start_cycle(number)
    result = {'cycle': number, 'entry': 'NOT_OBSERVED', 'linux_verified': False,
              'reset_attempts': 0, 'full_xnu_boot_claimed': False}
    reboot_intended, workers_stopped, bootstrap_attempted = False, True, False
    try:
        ops.save('verified-inputs.json', {'packet': packet, 'helper_source_sha256': SOURCE_SHA,
                                        'controller_sha256': sha(read_file(Path(__file__)))})
        baseline = check_linux(ops.observe())
        ops.save('baseline.json', baseline)
        ops.arm()
        ops.save('reboot-intent.json', {'command': 'orderly reboot', 'baseline_boot_id': baseline['boot_id']})
        reboot_intended = True
        ops.reboot()
        initial = wait_cdc(ops, 90)
        ops.save('bootstrap-port.json', initial)
        bootstrap_attempted = True
        workers_stopped = False
        bootstrap = ops.child('bootstrap', initial['callout'])
        workers_stopped = True
        ack = read_json(bootstrap / 'vector-ack.json')
        require(ack.get('status') == 'VECTOR_ACKNOWLEDGED_RUNTIME_UNVERIFIED' and
                ack.get('runtime_verified') is False, 'No bounded bootstrap vector acknowledgment')
        selected = wait_cdc(ops, 25, previous=initial)  # Require a new USB enumeration, even if the path is reused.
        ops.save('guest-port.json', {**selected, 'exact121_runtime_verified': False})
        workers_stopped = False
        guest = ops.child('guest', selected['callout'])
        workers_stopped = True
        if entry_observation(guest, packet):
            result['entry'] = 'ENTRY_PARK_OBSERVED'
    except (Exception, KeyboardInterrupt) as error:
        result['experiment_error'] = type(error).__name__ + ': ' + str(error)
    if not reboot_intended:
        result['status'] = 'ABORTED_BEFORE_REBOOT'
    elif not workers_stopped:
        result['status'] = 'WORKER_STATE_UNKNOWN_NO_RESET'
    else:
        try:
            if not bootstrap_attempted:
                # No proxy child was attempted. Let the window fall back; never reset Linux blindly.
                returned = recover_linux(ops, baseline['boot_id'])
            else:
                try:
                    returned = ops.observe()
                except (ConnectionError, TimeoutError):
                    returned = None
                if returned is not None:
                    check_linux(returned, previous=baseline['boot_id'])  # Healthy Linux is never PD-reset here.
                else:
                    # Failed SSH alone does not distinguish a proxy from booting Linux.
                    selected = ops.discover()
                    ops.save('recovery-cdc.json', {'primary_cdc': selected, 'linux_probe_failed': True})
                    if selected is not None:
                        ops.save('reset-intent.json', {'attempt': 1, 'workers_stopped': True,
                                 'acquisition_probes_stopped': True, 'primary_cdc': selected,
                                 'args': ['reboot', 'serial']})
                        result['reset_attempts'] = 1
                        ops.reset()
                    returned = recover_linux(ops, baseline['boot_id'])
            ops.save('returned-linux.json', returned)
            result['linux_verified'] = True
            ops.clear()
            result['status'] = result['entry'] + '_LINUX_VERIFIED'
        except (Exception, KeyboardInterrupt) as error:
            result.update(status='RECOVERY_INCOMPLETE_NO_RETRY', recovery_error=type(error).__name__ + ': ' + str(error))
            if isinstance(error, NativeResetTimeout):
                result.update(native_tool_killed=True, ace_state='unknown', target_reset_state='unknown')
    ops.save('result.json', result)
    return result


def run_cycles(ops, packet, count):
    results = []
    for number in range(1, count + 1):
        result = run_cycle(ops, packet, number)
        results.append(result)
        if result['status'] != 'ENTRY_PARK_OBSERVED_LINUX_VERIFIED' or result.get('experiment_error'):
            break
    return results


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute', action='store_true')
    p.add_argument('--confirm-recovery')
    p.add_argument('--cycles', type=int, choices=range(1, 4), default=1)
    p.add_argument('--manifest', type=Path, default=WORK / 'packet-r6/manifest.json')
    p.add_argument('--host-source', type=Path, default=WORK / 'm1n1')
    p.add_argument('--window-image', type=Path, default=WORK / 'window-004/m1n1-window.bin')
    p.add_argument('--inventory', type=Path, default=LAB / 'reports/sprint-007/ssh-target-inventory.json')
    p.add_argument('--private-root', type=Path, default=WORK / 'kernel-cycles-008')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    if not args.execute:
        print(json.dumps({'status': 'OFFLINE_PLAN_ONLY', 'cycles': args.cycles, 'device_contacted': False,
                          'inputs_verified': False, 'scope': 'bounded HV entry/park plus independent Linux recovery',
                          'full_xnu_boot_claimed': False, 'target_lock': str(LOCK)}))
        return 0
    try:
        require(args.confirm_recovery == CONFIRMATION, 'Explicit recovery confirmation required')
        require(not any(k.startswith('M1N1') for k in os.environ), 'm1n1 environment overrides refused')
        require(platform.system() == 'Darwin' and platform.machine() == 'arm64' and os.geteuid() == 0,
                'Execute requires root on M5 Darwin arm64')
        os.umask(0o077)
        with RealOps(args) as ops:
            packet = verify_local(args)  # No SSH or serial access before all local verification completes.
            results = run_cycles(ops, packet, args.cycles)
        complete = len(results) == args.cycles and all(r['status'] == 'ENTRY_PARK_OBSERVED_LINUX_VERIFIED' and
                                                     not r.get('experiment_error') for r in results)
        print(json.dumps({'status': 'BOUNDED_CYCLES_OBSERVED' if complete else 'STOPPED_FOR_REVIEW',
                          'cycles_recorded': len(results), 'full_xnu_boot_claimed': False}))
        return 0 if complete else 1
    except Exception:
        print(json.dumps({'status': 'CONTROLLER_STOPPED_REQUIRES_REVIEW', 'full_xnu_boot_claimed': False}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
