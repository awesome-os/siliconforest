#!/usr/bin/env python3
"""M5-local Linux halt/reset qualification. Offline unless --execute.

Exit 0 is an offline plan or recorded Linux-return observations. It is not a
hardware, m1n1, or XNU pass. Root sets up keys and runs --execute on the M5.
This process never reads the private key; ssh(1) opens IdentityFile only then.
"""
from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import select
import shlex
import stat
import subprocess
import sys
import termios
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
try:
    from .lab import ValidationError, read_json, regular_file
except ImportError:
    from lab import ValidationError, read_json, regular_file

Error = ValidationError
LAB = ROOT
TOOL_SHA = 'db6da3ff2f86d9663f748d975ce1ce988c82047014912018775b61f207b6ff4a'
SERIAL = '/dev/cu.debug-console'
LOADER = '/boot/efi/m1n1/boot.bin'
LOADER_SHA = '54fa07e4f5cbeecd90e67e335657c40b33c25aede7015a84afb1427333b4f923'
CANDIDATE_SHA = 'e4ead838bb87a04dd00909edf8a598f813056ee2f9611777a4a77c4bf7351e27'
ALLOWED_LOADER = frozenset({LOADER_SHA, CANDIDATE_SHA})
KERNEL = '7.2.11-6-1-ARCH'
M1_USER = 'chrisk'
M1_HOST = '10.1.1.127'
IDENTITY = '/Users/chriskearney/darwin-lab-firstboot-007/controller-ssh-008/id_ed25519'
KNOWN_HOSTS = '/Users/chriskearney/darwin-lab-firstboot-007/controller-ssh-008/known_hosts'
NATIVE = '/Users/chriskearney/darwin-lab-firstboot-007/native-control-src/macvdmtool'
TARGET_LOCK = Path('/Users/chriskearney/darwin-lab-firstboot-007/m1-controller-008.lock')
HOLD = 60
SETTLE = 20
AWAIT = 180
NEIGHBOR_WAIT = 25
POLL = 5
UNITS = (
    'lab-agent.service', 'maclab-lab-boot-session.service',
    'maclab-lab-boot-lock.service', 'maclab-lab-boot-deadlines.service',
)
PINS = {
    'ssh_identity': IDENTITY, 'known_hosts': KNOWN_HOSTS, 'm1_user': M1_USER,
    'm1_host': M1_HOST, 'expected_kernel': KERNEL, 'bootloader_path': LOADER,
    'native_tool': NATIVE, 'native_tool_sha256': TOOL_SHA, 'serial_device': SERIAL,
}
FIELDS = tuple(PINS) + ('bootloader_sha256', 'private_directory')
SAFE_DIR = re.compile(r'\A/[A-Za-z0-9_./+-]{1,400}\Z')
SERIAL_FLAGS = os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK
UNKNOWN = 'TARGET_UNKNOWN_NEEDS_INDEPENDENT_RESET'
ABORTED = 'ABORTED_BEFORE_HALT'


def require(ok, message):
    if not ok:
        raise Error(message)


def tool_digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_config(path: Path, hash_tool: bool = False) -> dict:
    raw = read_json(path)
    require(set(raw) == set(FIELDS), 'config keys do not match the recovery contract')
    cfg = {}
    for key, pinned in PINS.items():
        require(raw.get(key) == pinned, f'{key} is not the fixed qualification target')
        cfg[key] = pinned
    digest = raw.get('bootloader_sha256')
    require(digest in ALLOWED_LOADER, 'bootloader_sha256 is not a reviewed loader hash')
    cfg['bootloader_sha256'] = digest
    cfg['private_directory'] = str(private_dir(raw.get('private_directory')))
    for key in ('ssh_identity', 'known_hosts', 'native_tool'):
        regular_file(Path(cfg[key]))  # lstat only; identity bytes are never read
    if hash_tool:
        require(tool_digest(Path(NATIVE)) == TOOL_SHA, 'native tool bytes do not match db6da3ff')
    return cfg


def private_dir(value) -> Path:
    require(isinstance(value, str) and SAFE_DIR.fullmatch(value) and '..' not in value.split('/'),
            'private_directory is not a safe absolute path')
    path = Path(value)
    if path.is_symlink():
        raise Error('private directory is a symlink')
    resolved = path.resolve()
    require(resolved != LAB and LAB not in resolved.parents, 'private directory is inside the lab tree')
    keydir = Path(IDENTITY).resolve().parent
    require(resolved != keydir and keydir not in resolved.parents,
            'private directory must stay outside the key directory')
    return resolved


def ssh_argv(command: str) -> list[str]:
    return [
        'ssh', '-F', '/dev/null', '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes',
        '-o', 'IdentityAgent=none', '-o', 'StrictHostKeyChecking=yes',
        '-o', 'RequestTTY=no', '-o', 'ClearAllForwardings=yes', '-o', 'ConnectTimeout=8',
        '-o', f'IdentityFile={IDENTITY}', '-o', f'UserKnownHostsFile={KNOWN_HOSTS}',
        f'{M1_USER}@{M1_HOST}', command,
    ]


def remote_observe() -> str:
    """One root script. COMPAT lines stay split; the loader path is not config text."""
    quoted_loader = shlex.quote(LOADER)
    units = ' '.join(UNITS)
    script = (
        f'echo KERNEL=$(uname -r)\n'
        f"tr '\\000' '\\n' < /sys/firmware/devicetree/base/compatible | sed 's/^/COMPAT=/'\n"
        f"echo BOOTID=$(sha256sum /proc/sys/kernel/random/boot_id | awk '{{print $1}}')\n"
        f"echo LOADER=$(sha256sum {quoted_loader} | awk '{{print $1}}')\n"
        f'for u in {units}; do echo UNIT_$u=$(systemctl is-active "$u" || true); done\n'
        f'systemctl show -p RuntimeWatchdogUSec -p RebootWatchdogUSec\n'
        f'echo WD_FD=$(readlink /proc/1/fd/* 2>/dev/null | grep -c /dev/watchdog || true)\n'
        f'echo ROOTOPT=$(findmnt -n -o OPTIONS /)\n'
    )
    return 'sudo -n sh -c ' + shlex.quote(script)


def set_watchdogs(runtime: int, reboot: int) -> str:
    require(isinstance(runtime, int) and isinstance(reboot, int), 'watchdog values must be integers')
    require(0 <= runtime < 10**15 and 0 <= reboot < 10**15, 'watchdog value out of range')
    base = ('sudo -n busctl set-property org.freedesktop.systemd1 '
            '/org/freedesktop/systemd1 org.freedesktop.systemd1.Manager ')
    return (base + f'RuntimeWatchdogUSec t {runtime} && ' + base + f'RebootWatchdogUSec t {reboot}')


def usec(value: str) -> int:
    value = value.strip()
    for suffix, scale in (('us', 1), ('ms', 1_000), ('min', 60_000_000), ('s', 1_000_000)):
        if value.endswith(suffix) and value[:-len(suffix)]:
            return int(float(value[:-len(suffix)]) * scale)
    require(value.isdigit(), f'unparsed watchdog value {value!r}')
    return int(value)


def parse_block(text: str) -> dict:
    out = {}
    for line in text.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            out[key.strip()] = value.strip()
    return out


def compat_names(text: str) -> list[str]:
    names = []
    for line in text.splitlines():
        if line.startswith('COMPAT='):
            name = line.split('=', 1)[1].strip()
            if name:
                names.append(name)
    return names


def check_observe(text: str, expected_loader: str, prior_boot=None, prior_wdt=None, want_rw=False, want_fd=None) -> dict:
    fields = parse_block(text)
    names = compat_names(text)
    require(expected_loader in ALLOWED_LOADER, 'loader expectation is not a reviewed hash')
    require('apple,j293' in names and 'apple,t8103' in names, 'target is not J293/T8103')
    require(fields.get('KERNEL') == KERNEL, 'kernel does not match the known Linux')
    require(fields.get('LOADER') == expected_loader, 'bootloader hash does not match the configured reviewed loader')
    for unit in UNITS:
        require(fields.get(f'UNIT_{unit}') == 'inactive', f'{unit} is not inactive')
    boot = fields.get('BOOTID', '')
    require(len(boot) == 64 and all(c in '0123456789abcdef' for c in boot), 'boot id hash missing')
    pair = (usec(fields['RuntimeWatchdogUSec']), usec(fields['RebootWatchdogUSec']))
    fd = int(fields['WD_FD'])
    if want_fd is not None:
        require(fd == want_fd and pair == (0, 0), 'watchdog did not disarm')
    if prior_boot is not None:
        require(boot != prior_boot, 'boot id did not change')
    if prior_wdt is not None:
        require(pair == prior_wdt, 'watchdogs were not restored')
    if want_rw:
        require('rw' in set(fields.get('ROOTOPT', '').split(',')), 'root is not mounted rw')
    return {'boot_id_sha256': boot, 'runtime_usec': pair[0], 'reboot_usec': pair[1], 'wd_fd': fd}


def restore_watchdogs(ops, base: dict) -> bool:
    """True only after a zero ssh status and a readback of the original usec pair."""
    try:
        code, _ = ops.ssh(set_watchdogs(base['runtime_usec'], base['reboot_usec']), 20)
        if code != 0:
            return False
        code, text = ops.ssh(remote_observe(), 20)
        if code != 0:
            return False
        fields = parse_block(text)
        pair = (usec(fields['RuntimeWatchdogUSec']), usec(fields['RebootWatchdogUSec']))
        return pair == (base['runtime_usec'], base['reboot_usec'])
    except (Error, TimeoutError, OSError, KeyError, ValueError):
        return False


def guard_native(calls: int, args) -> None:
    require(list(args) == ['reboot', 'serial'], 'refusing native command other than reboot serial')
    require(calls == 0, 'refusing a second native call')


def require_host(system: str, machine: str, euid: int) -> None:
    require(system == 'Darwin' and machine == 'arm64', 'execute is Darwin arm64 M5 only')
    require(euid == 0, 'execute must run as root on the M5')


def acquire_lock(path: Path):
    import fcntl
    if Path(path).is_symlink():
        raise Error('controller lock is a symlink')
    handle = open(path, 'a')
    try:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as exc:
        handle.close()
        if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
            raise Error('another controller holds the lock') from exc
        raise
    return handle


def release_lock(handle) -> None:
    import fcntl
    if handle is None:
        return
    try:
        fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
    finally:
        handle.close()


def prepare_private(path: Path) -> None:
    if path.is_symlink():
        raise Error('private directory is a symlink')
    parent = path.parent
    require(not parent.is_symlink() and parent.is_dir(), 'private directory parent is missing or a symlink')
    require(not path.exists() or path.is_dir(), 'private directory is not a directory')
    path.mkdir(mode=0o700, exist_ok=True)
    require(not path.is_symlink(), 'private directory is a symlink')
    os.chmod(path, 0o700)
    require(stat.S_IMODE(path.lstat().st_mode) == 0o700, 'private directory mode is not 0700')


def open_private(path: Path) -> int:
    if path.is_symlink():
        raise Error('refusing a symlink for a private file')
    flags = os.O_CREAT | os.O_APPEND | os.O_WRONLY | os.O_NOFOLLOW
    try:
        fd = os.open(str(path), flags, 0o600)
    except OSError as exc:
        raise Error('could not open a private file') from exc
    try:
        os.fchmod(fd, 0o600)
        require(stat.S_IMODE(os.fstat(fd).st_mode) == 0o600, 'private file mode is not 0600')
        return fd
    except Exception:
        os.close(fd)
        raise


def raw_115200(attrs):
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    cc = list(cc)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    cflag = (cflag & ~termios.CSIZE & ~termios.PARENB & ~termios.CSTOPB) | termios.CS8 | termios.CREAD | termios.CLOCAL
    return [0, 0, cflag, 0, termios.B115200, termios.B115200, cc]


def configure_serial(fd: int) -> None:
    termios.tcsetattr(fd, termios.TCSANOW, raw_115200(termios.tcgetattr(fd)))


def spawn_reader(fd: int, log_path: Path):
    log_fd = open_private(log_path)
    stop = threading.Event()

    def pump():
        try:
            while not stop.is_set():
                ready, _, _ = select.select([fd], [], [], 0.2)
                if not ready:
                    continue
                chunk = os.read(fd, 4096)
                if chunk:
                    os.write(log_fd, chunk)
        finally:
            os.close(log_fd)
            os.close(fd)

    thread = threading.Thread(target=pump, name='m1-recovery-serial', daemon=False)
    thread.start()
    return stop, thread


def stop_reader(stop, thread) -> None:
    if stop is not None:
        stop.set()
    if thread is not None:
        thread.join(timeout=5)


def read_last_record(path: Path):
    if not path.exists():
        return None
    if path.is_symlink():
        raise Error('journal is a symlink')
    require(path.stat().st_size <= 1_000_000, 'journal is unexpectedly large')
    last = None
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.strip():
            try:
                last = json.loads(line)
            except json.JSONDecodeError as exc:
                raise Error('journal line is not JSON') from exc
    return last


def cycle_blocked(last) -> bool:
    if not last:
        return False
    if last.get('native_calls') or last.get('phase') == 'native_requested':
        return True
    if last.get('status') == UNKNOWN:
        return True
    if last.get('phase') == 'halt_requested':
        return not (last.get('status') == ABORTED and last.get('watchdogs_restored') is True)
    return False


def _poll(ops, seconds: float, until_down: bool) -> bool:
    start = ops.now()
    while True:
        down = not ops.reachable()
        if until_down and down:
            return True
        if not until_down and not down:
            return False
        if ops.now() - start >= seconds:
            return down
        ops.sleep(min(POLL, seconds - (ops.now() - start)))


class RealOps:
    def __init__(self, cfg):
        require_host(platform.system(), platform.machine(), os.geteuid())
        self.cfg = cfg
        self.native_calls = 0
        self._lock = None
        self._serial = (None, None)
        private = Path(cfg['private_directory'])
        prepare_private(private)
        self.journal = private / 'journal.jsonl'
        self.serial_path = private / 'serial.log'
        self._lock = acquire_lock(TARGET_LOCK)

    def lock(self):
        require(self._lock is not None, 'controller lock is not held')

    def unlock(self):
        release_lock(self._lock)
        self._lock = None

    def ssh(self, command: str, timeout: float):
        try:
            done = subprocess.run(ssh_argv(command), timeout=timeout, capture_output=True, text=True)
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError('ssh timed out') from exc
        return done.returncode, done.stdout

    def reachable(self) -> bool:
        try:
            code, _ = self.ssh('true', 8)
        except (TimeoutError, OSError):
            return False
        return code == 0

    def native(self, args, timeout: float):
        guard_native(self.native_calls, args)
        self.native_calls += 1
        try:
            done = subprocess.run([NATIVE, *args], timeout=timeout, capture_output=True, text=True)
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError('native tool timed out') from exc
        require(done.returncode == 0, 'macvdmtool reboot serial failed')
        return done.returncode

    def selected_neighbor_refresh(self):
        """One arp delete for the known M1. Connection refused after PD reset can be a stale neighbor."""
        require(not getattr(self, '_neighbor_refreshed', False), 'refusing a second neighbor refresh')
        self._neighbor_refreshed = True
        argv = ['/usr/sbin/arp', '-d', M1_HOST]
        require(argv == ['/usr/sbin/arp', '-d', '10.1.1.127'], 'neighbor refresh is only 10.1.1.127')
        try:
            done = subprocess.run(argv, timeout=5, capture_output=True, text=True)
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError('neighbor refresh timed out') from exc
        return done.returncode

    def sleep(self, seconds: float):
        time.sleep(seconds)

    def now(self) -> float:
        return time.monotonic()

    def serial_begin(self):
        if os.path.islink(SERIAL):
            raise Error('serial device is a symlink')
        fd = os.open(SERIAL, SERIAL_FLAGS)
        try:
            configure_serial(fd)
            self._serial = spawn_reader(fd, self.serial_path)
        except Exception:
            os.close(fd)
            raise

    def serial_end(self):
        stop, thread = self._serial
        self._serial = (None, None)
        stop_reader(stop, thread)

    def last_journal(self):
        return read_last_record(self.journal)

    def write(self, record: dict):
        payload = (json.dumps(record, sort_keys=True) + '\n').encode()
        fd = open_private(self.journal)
        try:
            offset = 0
            while offset < len(payload):
                wrote = os.write(fd, payload[offset:])
                if wrote <= 0:
                    raise OSError('journal write did not advance')
                offset += wrote
            os.fsync(fd)
        finally:
            os.close(fd)


def _finish(ops, record: dict) -> None:
    try:
        ops.serial_end()
    finally:
        try:
            record['native_calls'] = max(int(record.get('native_calls') or 0), int(getattr(ops, 'native_calls', 0) or 0))
            ops.write(record)
        except OSError as exc:
            record['journal_error'] = type(exc).__name__
        finally:
            ops.unlock()


def _blocked(ops, record, reason: str) -> dict:
    record['status'] = UNKNOWN
    record['phase'] = 'native_requested'
    record['native_calls'] = max(1, int(getattr(ops, 'native_calls', 0) or 0))
    record['reason'] = reason
    record['watchdogs_restored'] = False
    return record


def run_cycle(cfg: dict, ops) -> dict:
    ops.lock()
    record = {
        'hardware_boot_observed': False, 'cpu_halt_independently_observed': False,
        'claim': 'linux_halt_reset_observations', 'native_calls': 0, 'blind_retry': False,
        'phase': 'pre_halt', 'watchdogs_restored': False,
    }
    try:
        if ops.native_calls:
            return _blocked(ops, record, 'native call already used')
        if cycle_blocked(ops.last_journal()):
            return _blocked(ops, record, 'prior journal blocks another cycle')
        ops.serial_begin()
        code, text = ops.ssh(remote_observe(), 20)
        require(code == 0, 'preflight ssh failed')
        expected = cfg['bootloader_sha256']
        base = check_observe(text, expected)
        record['baseline'] = base
        code, _ = ops.ssh(set_watchdogs(0, 0), 20)
        require(code == 0, 'could not disarm watchdogs')
        code, disarmed = ops.ssh(remote_observe(), 20)
        require(code == 0, 'disarm readback failed')
        check_observe(disarmed, expected, want_fd=0)
        record['phase'] = 'halt_requested'
        try:
            ops.write(record)  # durable before halt; a crash must not look unattempted
        except (Error, OSError) as exc:
            record['phase'] = 'pre_halt'
            record['status'] = ABORTED
            record['error'] = str(exc)
            record['watchdogs_restored'] = restore_watchdogs(ops, base) if ops.reachable() else False
            return record
        try:
            ops.ssh('sudo -n systemctl halt', 25)
        except TimeoutError:
            pass
        if not _poll(ops, SETTLE, until_down=True):
            record['watchdogs_restored'] = restore_watchdogs(ops, base)
            record['status'] = ABORTED
            return record
        if not _poll(ops, HOLD, until_down=False):
            record['status'] = UNKNOWN
            record['reason'] = 'target returned during the hold; no reset sent'
            return record
        record['phase'] = 'native_requested'
        record['native_calls'] = 1
        try:
            ops.write(record)  # durable before the only reset
        except (Error, OSError) as exc:
            record['phase'] = 'halt_requested'
            record['native_calls'] = 0
            record['status'] = UNKNOWN
            record['watchdogs_restored'] = False
            record['error'] = str(exc)
            record['reason'] = 'journal failed before reset; no reset sent'
            return record
        try:
            ops.native(['reboot', 'serial'], 40)
        except TimeoutError as exc:
            record['status'] = UNKNOWN
            record['watchdogs_restored'] = False
            record['error'] = str(exc)
            record['reason'] = 'native tool timed out; ACE state unknown; no retry'
            return record
        start = ops.now()
        refreshed = False
        while not ops.reachable():
            elapsed = ops.now() - start
            if elapsed >= AWAIT:
                record['status'] = UNKNOWN
                record['reason'] = 'no SSH after the one reboot serial'
                return record
            if not refreshed and elapsed >= NEIGHBOR_WAIT:
                refreshed = True
                try:
                    code = ops.selected_neighbor_refresh()
                except (TimeoutError, OSError) as exc:
                    record['neighbor_refresh'] = {'attempted': True, 'returncode': None, 'error': str(exc)}
                else:
                    record['neighbor_refresh'] = {'attempted': True, 'returncode': code}
                continue
            ops.sleep(min(POLL, AWAIT - elapsed))
        code, after = ops.ssh(remote_observe(), 20)
        require(code == 0, 'return ssh failed')
        record['returned'] = check_observe(
            after, expected, prior_boot=base['boot_id_sha256'],
            prior_wdt=(base['runtime_usec'], base['reboot_usec']), want_rw=True)
        record['status'] = 'OBSERVATIONS_RECORDED'
        return record
    except (Error, TimeoutError, OSError) as exc:
        sent = record.get('phase') == 'native_requested' or getattr(ops, 'native_calls', 0) > 0
        if sent or record.get('phase') == 'halt_requested':
            record['status'] = UNKNOWN
            record['watchdogs_restored'] = False
        else:
            record['status'] = ABORTED
            if 'baseline' in record and ops.reachable():
                record['watchdogs_restored'] = restore_watchdogs(ops, record['baseline'])
            else:
                record['watchdogs_restored'] = False
        record['error'] = str(exc)
        return record
    finally:
        _finish(ops, record)


def plan() -> dict:
    return {
        'status': 'OFFLINE_PLAN_ONLY', 'hardware_boot_observed': False,
        'm1': f'{M1_USER}@{M1_HOST}', 'kernel': KERNEL, 'bootloader_path': LOADER,
        'allowed_bootloader_sha256': [LOADER_SHA, CANDIDATE_SHA],
        'target_lock': str(TARGET_LOCK), 'serial_device': SERIAL,
        'native_tool_sha256': TOOL_SHA, 'hold_seconds': HOLD, 'settle_seconds': SETTLE,
        'ssh_options': ['BatchMode=yes', 'IdentitiesOnly=yes', 'IdentityAgent=none',
                        'StrictHostKeyChecking=yes', 'RequestTTY=no', 'ClearAllForwardings=yes'],
        'actions': [
            'sudo -n sh -c observes J293, T8103, known Linux, loader hash, inactive old units',
            'record boot-id hash and runtime watchdog usec; count pid1 watchdog fds without opening them',
            'set RuntimeWatchdogUSec and RebootWatchdogUSec to 0 and require the fd count to be 0',
            'sudo -n systemctl halt', f'hold observed-down {HOLD}s after SSH drops',
            'one macvdmtool reboot serial',
            'require a new boot id, same kernel and loader, restored watchdogs, rw root',
        ],
        'limits': [
            'not a hardware, m1n1, or XNU pass', 'no DFU, power-off, or second reset',
            'after halt is requested, failure stays unknown and is not retried',
            'one arp -d 10.1.1.127 if SSH is still down 25s after the reset; no second reset',
            'native timeout leaves ACE state unknown and is not retried',
            'watchdog restore counts only when ssh returns 0 and readback matches',
            'one shared target lock; a held lock refuses this controller',
        ],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args(argv)
    try:
        cfg = load_config(args.config, hash_tool=args.execute)
        if not args.execute:
            print(json.dumps(plan(), indent=2))
            return 0
        result = run_cycle(cfg, RealOps(cfg))
        print(json.dumps(result, indent=2))
        return 0 if result.get('status') == 'OBSERVATIONS_RECORDED' else 1
    except (Error, OSError) as exc:
        print(json.dumps({'status': 'REJECTED', 'hardware_boot_observed': False, 'error': str(exc)}))
        return 1


if __name__ == '__main__':
    sys.exit(main())
