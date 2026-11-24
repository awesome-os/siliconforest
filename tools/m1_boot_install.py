#!/usr/bin/env python3
"""One reviewed J293 stage2 proposal; plan by default, no reboot or server calls.

This writes boot storage only with --execute. It never changes stage1, NVRAM,
partitions or the existing boot.bin.old. A successful write is not a boot test.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import stat
import struct
import subprocess
import sys

ORIGINAL_SHA = '54fa07e4f5cbeecd90e67e335657c40b33c25aede7015a84afb1427333b4f923'
CANDIDATE_SHA = 'e4ead838bb87a04dd00909edf8a598f813056ee2f9611777a4a77c4bf7351e27'
PREFIX_SHA = 'ba5bf4b396168ac540df852e3507c4342dd289eef7bf2c1fc29c50f11f0822bf'
PREFIX_BYTES = 0x420000
TAG = b'##m1n1_ver##121fcbf35d4501814b58ca5347926a4173b6a512-darwin-lab-j293-window-v1\0'
ESP = Path('/boot/efi')
TARGET = ESP / 'm1n1/boot.bin'
BACKUP = TARGET.with_name('boot.bin.darwin-lab-original-54fa07e4f5cb')
STATE = Path('/var/lib/darwin-lab/boot-window-008')
MAX_BYTES = 32 << 20


class BootFileUnverified(ValueError):
    """A replacement happened but final verification/receipt did not finish."""


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regular_bytes(path):
    path = Path(path).absolute()
    require(path.resolve(strict=True) == path, 'Symlink path refused: ' + str(path))
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and 0 < info.st_size <= MAX_BYTES,
            'Expected bounded regular file: ' + str(path))
    return path.read_bytes()


def footprint(data):
    require(len(data) >= 0x828 and data[0x808:0x810] == b'AURALOAD', 'Missing raw load header')
    version, flags, size, base = struct.unpack_from('<IIQQ', data, 0x810)
    require(version == flags == 1 and base == 0 and size % 0x4000 == 0,
            'Unsupported raw image header')
    return size


def validate_payload(original, candidate=None):
    require(sha(original) == ORIGINAL_SHA and footprint(original) == 0x400000,
            'Original recovery image differs from the reviewed backup')
    if candidate is not None:
        require(sha(candidate) == CANDIDATE_SHA and footprint(candidate) == PREFIX_BYTES,
                'Candidate differs from the reviewed proposal')
        require(sha(candidate[:PREFIX_BYTES]) == PREFIX_SHA and
                candidate[:PREFIX_BYTES].count(TAG) == 1, 'Candidate prefix/tag mismatch')
        require(candidate[PREFIX_BYTES:] == original[0x400000:], 'Original Linux payload changed')


def validate_boot_identity(partuuid, chosen_esp, chosen_version):
    require(partuuid.strip().lower() == chosen_esp.rstrip(b'\0').decode('ascii').lower(),
            'Mounted ESP differs from the booted stage1 ESP')
    require(chosen_version.rstrip(b'\0') in
            (b'f5a82fdb-dirty', TAG[len(b'##m1n1_ver##'):-1]),
            'Booted stage2 version is not the original or reviewed candidate')


def validate_machine():
    require(sys.platform == 'linux' and platform.machine() == 'aarch64' and os.geteuid() == 0,
            'Run on the selected Linux M1 as root')
    compatible = Path('/sys/firmware/devicetree/base/compatible').read_bytes().split(b'\0')
    require({b'apple,j293', b'apple,t8103'} <= set(compatible), 'Target is not J293/T8103')
    mounts = [row.split() for row in Path('/proc/mounts').read_text().splitlines()]
    matching = [row for row in mounts if row[1] == str(ESP)]
    require(len(matching) == 1 and matching[0][2] == 'vfat' and
            'rw' in matching[0][3].split(','), 'Expected a writable mounted FAT ESP')
    require(TARGET.resolve(strict=True) == TARGET and TARGET.stat().st_uid == 0,
            'Unexpected boot-file path or owner')
    chosen = Path('/sys/firmware/devicetree/base/chosen')
    partuuid = subprocess.run(['findmnt', '-n', '-o', 'PARTUUID', '--target', str(ESP)],
                              check=True, capture_output=True, text=True, timeout=5).stdout
    require(re.fullmatch(r'[0-9a-fA-F-]{36}\s*', partuuid), 'Missing mounted ESP PARTUUID')
    validate_boot_identity(partuuid, (chosen / 'asahi,efi-system-partition').read_bytes(),
                           (chosen / 'asahi,m1n1-stage2-version').read_bytes())


def check_interrupted_operations():
    require(not list(TARGET.parent.glob('.boot.bin.darwin-lab-*.tmp')),
            'Unreviewed staged boot file remains; inspect before another operation')
    if STATE.exists():
        require(STATE.resolve() == STATE, 'Symlink operation state refused')
        for intent in STATE.glob('*/intent.json'):
            require((intent.parent / 'result.json').is_file(),
                    'Prior boot operation is incomplete; inspect before another operation')


def inspect(action, original_path, candidate_path=None):
    validate_machine()
    check_interrupted_operations()
    original = regular_bytes(original_path)
    candidate = regular_bytes(candidate_path) if action == 'install' else None
    validate_payload(original, candidate)
    expected, desired = ((ORIGINAL_SHA, CANDIDATE_SHA) if action == 'install'
                         else (CANDIDATE_SHA, ORIGINAL_SHA))
    current = sha(regular_bytes(TARGET))
    require(current in (expected, desired), 'Current boot file drifted; no force/overwrite path')
    require(not BACKUP.is_symlink(), 'Symlink recovery backup refused')
    if BACKUP.exists():
        require(sha(regular_bytes(BACKUP)) == ORIGINAL_SHA, 'Existing recovery backup differs')
    space = os.statvfs(ESP)
    free = space.f_bavail * space.f_frsize
    data = candidate if action == 'install' else original
    require(free >= len(data) + (0 if BACKUP.exists() else len(original)) + (16 << 20),
            'Insufficient space for a verified backup and staged replacement')
    return {'status': 'PLAN_ONLY_NO_BOOT_STORAGE_WRITES', 'action': action,
            'target': str(TARGET), 'backup': str(BACKUP), 'current_sha256': current,
            'required_current_sha256': expected, 'proposed_sha256': desired,
            'proposed_bytes': len(data), 'esp_free_bytes': free,
            'already_at_desired_bytes': current == desired, 'hardware_boot_validated': False}, original, data


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def exclusive_write(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'wb') as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def verify_after_sync(path, expected):
    # fsync flushes the file; DONTNEED asks Linux to discard its clean cached pages
    # so this verification re-reads storage. It is not a power-loss durability test.
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
        os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
    finally:
        os.close(fd)
    require(sha(regular_bytes(path)) == expected,
            'BOOT_FILE_UNVERIFIED_DO_NOT_REBOOT: storage re-read differs: ' + str(path))


def execute(plan, original, data, operation_id):
    require(re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', operation_id), 'Invalid operation ID')
    if plan['already_at_desired_bytes']:
        return {**plan, 'status': 'NO_CHANGE_ALREADY_AT_DESIRED_BYTES'}
    os.umask(0o077)
    STATE.mkdir(parents=True, mode=0o700, exist_ok=True)
    require(STATE.resolve() == STATE and STATE.stat().st_uid == os.geteuid() and
            STATE.stat().st_mode & 0o077 == 0, 'State directory must be private and root-owned')
    sync_directory(STATE.parent)
    sync_directory(STATE.parent.parent)
    job = STATE / operation_id
    job.mkdir(mode=0o700)  # Durable one-shot operation; never silently retry an existing intent.
    exclusive_write(job / 'intent.json', (json.dumps(plan, indent=2) + '\n').encode())
    sync_directory(job)
    sync_directory(STATE)
    require(sha(regular_bytes(TARGET)) == plan['required_current_sha256'], 'CAS drift before backup')
    if not BACKUP.exists():
        exclusive_write(BACKUP, original)
        sync_directory(BACKUP.parent)
    require(sha(regular_bytes(BACKUP)) == ORIGINAL_SHA, 'Recovery backup verification failed')
    verify_after_sync(BACKUP, ORIGINAL_SHA)
    staged = TARGET.with_name('.boot.bin.darwin-lab-' + operation_id + '.tmp')
    exclusive_write(staged, data)
    require(sha(regular_bytes(staged)) == plan['proposed_sha256'], 'Staged bytes differ')
    require(sha(regular_bytes(TARGET)) == plan['required_current_sha256'], 'CAS drift before rename')
    os.replace(staged, TARGET)
    try:
        sync_directory(TARGET.parent)
        verify_after_sync(TARGET, plan['proposed_sha256'])
        result = {**plan, 'status': 'BOOT_FILE_REPLACED_NOT_BOOT_VALIDATED', 'operation_id': operation_id}
        exclusive_write(job / 'result.json', (json.dumps(result, indent=2) + '\n').encode())
        sync_directory(job)
    except Exception as error:
        raise BootFileUnverified('BOOT_FILE_UNVERIFIED_DO_NOT_REBOOT: ' + str(error)) from error
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action', choices=('install', 'restore'), default='install')
    parser.add_argument('--original', required=True, type=Path)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--operation-id')
    args = parser.parse_args(argv)
    try:
        require(args.action != 'install' or args.candidate is not None, 'Install needs the reviewed candidate')
        plan, original, data = inspect(args.action, args.original, args.candidate)
        if args.execute:
            require(args.operation_id is not None, 'Execution needs a unique operation ID')
            plan = execute(plan, original, data, args.operation_id)
        print(json.dumps(plan, indent=2))
        return 0
    except Exception as error:
        status = ('BOOT_FILE_UNVERIFIED_DO_NOT_REBOOT' if isinstance(error, BootFileUnverified)
                  else 'REFUSED_OR_INCOMPLETE_NO_BOOT_VERDICT')
        print(json.dumps({'status': status, 'error': str(error),
                          'hardware_boot_validated': False}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
