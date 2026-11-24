#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host launcher/collector for one finite private M1 Firefox run.

Caller must hold runs/m1-coord/gui.lock and check the coordination gate first.
All runner options after -- are forwarded as literal argv, including URL queries.
No installed file or owner profile is written. AVD prewarm is diagnostic only.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import tarfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', required=True)
    ap.add_argument('--tools', required=True)
    ap.add_argument('--installed-bundle', action='store_true', help='diagnose only /Applications/Firefox.app with a fresh private profile; never install or modify it')
    ap.add_argument('--bundle', default='/private/var/db/sffx/baseline-160-001/Firefox.app')
    ap.add_argument('--avd-prewarm', action='store_true')
    ap.add_argument('--avd-counters', action='store_true')
    ap.add_argument('options', nargs=argparse.REMAINDER)
    args = ap.parse_args()
    if not re.fullmatch('[a-z0-9][a-z0-9-]{0,70}', args.run):
        raise ValueError('Expected a fresh simple run name')
    if not args.tools.startswith('/private/var/db/sffx/') or '..' in Path(args.tools).parts:
        raise ValueError('Tools must be in the private target prefix')
    repo = Path(__file__).resolve().parents[3]
    out = repo / 'runs/firefox' / args.run
    remote = '/private/var/db/sffx/' + args.run
    tool_path, run_path = Path(args.tools), Path(remote)
    if tool_path.is_relative_to(run_path) or run_path.is_relative_to(tool_path):
        raise ValueError('Tool and run directories must be separate, non-overlapping prefixes')
    if out.exists():
        raise FileExistsError(out)
    out.mkdir(parents=True)
    ssh = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlias=m1-siliconforest', '-o',
           'UserKnownHostsFile=' + str(Path.home() / '.ssh/known_hosts_m1_siliconforest'),
           'root@10.1.1.211']
    options = args.options[1:] if args.options[:1] == ['--'] else args.options
    if args.installed_bundle and args.bundle != '/Applications/Firefox.app':
        raise ValueError('Installed diagnostics accept only /Applications/Firefox.app')
    if not args.installed_bundle and (not args.bundle.startswith('/private/var/db/sffx/') or '..' in Path(args.bundle).parts or not args.bundle.endswith('.app')):
        raise ValueError('Bundle must be an app inside the private prefix')
    command = ['python3', args.tools + '/remote-run.py', '--bundle', args.bundle, '--run', remote] + options
    seconds = 40
    for index, option in enumerate(options):
        if option == '--seconds' and index + 1 < len(options):
            seconds = int(options[index + 1])
        elif option.startswith('--seconds='):
            seconds = int(option.partition('=')[2])
    if not 1 <= seconds <= 3600:
        raise ValueError('Owned run duration must be1..3600seconds')
    metadata = dict(command=command, avd_prewarm_diagnostic=args.avd_prewarm)
    if args.avd_prewarm:
        script = 'import os; fd=os.open("/dev/sfavd",os.O_RDWR); print("diagnostic_root_AVD_prewarm_fd",fd); os.close(fd)'
        r = subprocess.run(ssh + [shlex.join(['python3', '-c', script])], capture_output=True, text=True, timeout=20)
        (out / 'avd-prewarm.log').write_text(r.stdout + r.stderr)
        metadata['avd_prewarm_status'] = r.returncode
    if args.avd_counters:
        r = subprocess.run(ssh + ['sysctl kern.sf_avd.stats'], capture_output=True, text=True, timeout=10)
        (out / 'avd-before.log').write_text(r.stdout + r.stderr)
    try:
        with (out / 'launcher.log').open('w') as log:
            r = subprocess.run(ssh + [shlex.join(command)], stdout=log, stderr=subprocess.STDOUT, timeout=max(180, seconds + 90))
            metadata['runner_status'] = r.returncode
    finally:
        if args.avd_counters:
            r = subprocess.run(ssh + ['sysctl kern.sf_avd.stats'], capture_output=True, text=True, timeout=10)
            (out / 'avd-after.log').write_text(r.stdout + r.stderr)
        (out / 'launch-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        # Preserve evidence on nonzero launcher exit too. Prefix is owned and fixed.
        archive = out / 'target-evidence.tar.gz'
        with archive.open('wb') as stream:
            copied = subprocess.run(ssh + [shlex.join(['tar', '-C', remote, '-czf', '-', '.'])],
                                    stdout=stream, stderr=subprocess.PIPE, timeout=60)
        if copied.returncode == 0:
            with tarfile.open(archive) as tar:
                # An isolated integration home deliberately has an absolute
                # Applications symlink. Preserve its metadata as JSON, never
                # recreate links or let them redirect host evidence extraction.
                members = tar.getmembers()
                links = [dict(name=m.name, target=m.linkname, kind='symlink' if m.issym() else 'hardlink')
                         for m in members if m.issym() or m.islnk()]
                (out / 'evidence-links.json').write_text(json.dumps(links, indent=2) + '\n')
                tar.extractall(out, members=[m for m in members if not m.issym() and not m.islnk()], filter='data')
        else:
            (out / 'collection-error.log').write_bytes(copied.stderr)
    print(json.dumps(dict(run=args.run, output=str(out), runner_status=metadata.get('runner_status'))))
    raise SystemExit(metadata['runner_status'])


if __name__ == '__main__':
    main()
