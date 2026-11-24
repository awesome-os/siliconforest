#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Boot SiliconForest in QEMU (arm64 `virt`, TCG; the qemu-dev stream's image and runner,
used unmodified) with a Homebrew prefix disk mounted read-write at /opt/homebrew, run the
smoke tests of hb_tests.py in the guest and record exactly what each one did.

  hb_qemu.py --prefix first --name r1 [--only jq,tree] [--overlay DIR ...]

  system image   a private copy of the qemu-dev template (runs/qemu-dev/base/template-<tip|pin>-release-minimal-*.img:
                 upstream's arm64-virt-minimal RELEASE image, the lab's kernel collection, loader and
                 libSystem), plus /usr/bin/hb-guest, the test scripts under /usr/share/hb/, and every
                 file of each --overlay directory (a tree rooted at /: usr/lib/libz.1.dylib ...).
                 The overlays are how gap-closing libraries reach the guest.
  prefix disk    runs/homebrew/prefix/<prefix>.img from hb_pour.py, second virtio disk, persistent.
  network        QEMU user networking (the runner's default virtio-net).

Results: runs/homebrew/qemu/<name>/results.json and results.md; the guest console is serial.log.
Exit status 0 when the guest ran to the end (whatever the tests said), 2 otherwise.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
RUNS = LAB / 'runs/homebrew'
QD = LAB / 'integrations/siliconforest/qemu-dev'
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(QD))
import hb_tests  # noqa: E402

GUEST = r'''#!/bin/sh
# Mount the Homebrew prefix disk and run the tests. Typed from the serial shell by hb_qemu.py.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo HB_BEGIN
mkdir -p /opt/homebrew /tmp
mounted=no
for d in /dev/disk0s1 /dev/disk1s1 /dev/disk2s1; do
  [ -e $d ] || continue
  if mount -t ext4 $d /opt/homebrew 2>/dev/null; then
    if [ -d /opt/homebrew/Cellar ]; then mounted=$d; break; fi
    umount /opt/homebrew
  fi
done
echo "HB prefix disk: $mounted"
mount | sed 's/^/HB mount: /'
# Network: the qemu-dev image's own bring-up (DHCP on QEMU's user network), when it has one.
if [ -x /usr/bin/qemu-net-up ]; then echo "HB net: $(qemu-net-up en0 2>&1 | tail -2 | tr '\n' ' ')"; fi
echo "HB sw_vers: $(sw_vers 2>&1 | tr '\n' ' ')"
echo "HB uname: $(uname -a)"
[ "$mounted" = no ] || sh /usr/share/hb/run.sh
sync
umount /opt/homebrew 2>/dev/null
echo HB_END
'''

RUN_HEAD = r'''#!/bin/sh
PATH=/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/sbin:/usr/bin:/usr/sbin; export PATH
HOME=/tmp/hbhome; export HOME; mkdir -p $HOME
TERM=xterm-256color; export TERM
LC_ALL=C; export LC_ALL
run() {
  name=$1; secs=$2
  echo "HBT_BEGIN $name"
  rm -rf /tmp/hbt; mkdir -p /tmp/hbt; cd /tmp/hbt
  sh /usr/share/hb/t/$name.sh > /tmp/hbt.out 2>&1 &
  pid=$!
  ( sleep $secs; echo "HBT_TIMEOUT $name"; kill -9 $pid ) &
  w=$!
  wait $pid; rc=$?
  kill $w 2>/dev/null; wait $w 2>/dev/null
  head -c 2500 /tmp/hbt.out; echo
  echo "HBT_END $name rc=$rc"
  if [ $rc -ge 129 ] && [ $rc -ne 137 ] && [ -f /usr/lib/libhbcrash.dylib ]; then
    # Once more with the crash reporter inserted, for the backtrace only.
    rm -rf /tmp/hbt; mkdir -p /tmp/hbt; cd /tmp/hbt
    echo "HBT_CRASH_BEGIN $name"
    DYLD_INSERT_LIBRARIES=/usr/lib/libhbcrash.dylib sh /usr/share/hb/t/$name.sh 2>&1 | grep hbcrash: | head -40
    echo "HBT_CRASH_END $name"
  fi
  cd /
}
'''


def classify(rc, out, timed_out):
    """Name the failure exactly as the guest reported it."""
    if timed_out:
        return 'timeout'
    m = re.search(r'dyld(?:\[\d+\])?: (Library not loaded: \S+|Symbol not found: \S+)', out)
    if m:
        extra = re.search(r'(Referenced from|Expected in): [^\n]{0,200}', out)
        return 'dyld: ' + m.group(1) + (' (%s)' % extra.group(0) if extra else '')
    if rc is None:
        return 'did not finish'
    if rc >= 128:
        names = {4: 'SIGILL', 6: 'SIGABRT', 9: 'SIGKILL', 10: 'SIGBUS', 11: 'SIGSEGV', 12: 'SIGSYS', 5: 'SIGTRAP', 13: 'SIGPIPE'}
        return 'killed by ' + names.get(rc - 128, 'signal %d' % (rc - 128))
    if rc == 127 and 'not found' in out:
        return 'command not found'
    if rc == 126:
        return 'cannot execute'
    return 'exit %d' % rc if rc else 'wrong output'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--prefix', required=True, help='prefix image name (runs/homebrew/prefix/<name>.img)')
    p.add_argument('--name', required=True, help='run directory runs/homebrew/qemu/<name>')
    p.add_argument('--only', default='', help='comma-separated test names or formulae')
    p.add_argument('--overlay', type=Path, action='append', default=[], help='directory tree copied onto the system root')
    p.add_argument('--extra-script', type=Path, help='shell script run in the guest after the tests (its output is in serial.log)')
    p.add_argument('--timeout', type=int, default=0, help='whole run, seconds (default: sum of the tests\' limits + 300)')
    p.add_argument('--memory', type=int, default=3072)
    p.add_argument('--smp', type=int, default=4)
    p.add_argument('--refresh-system', action='store_true', help='take a new copy of the qemu-dev template')
    a = p.parse_args()

    out = RUNS / 'qemu' / a.name
    out.mkdir(parents=True, exist_ok=True)
    prefix_img = RUNS / 'prefix' / (a.prefix + '.img')
    poured = set(json.loads((RUNS / 'prefix' / (a.prefix + '.json')).read_text())['formulae'])
    only = set(filter(None, a.only.split(',')))
    tests = [t for t in hb_tests.TESTS if t[1] in poured and (not only or t[0] in only or t[1] in only)]

    # The system image is the qemu-dev template of the SiliconForest source in use (../rebase/pd.nix:
    # the tip unless SF_PD=pin). The pin's copy keeps the name it had before the source could be
    # chosen, so earlier runs' base stays where it was.
    source = os.environ.get('SF_PD') or 'tip'
    if source not in ('pin', 'tip'):
        raise SystemExit('SF_PD=%s: expected pin or tip' % source)
    stem = 'system-base' if source == 'pin' else 'system-base-' + source
    base = RUNS / 'qemu' / (stem + '.img')
    if a.refresh_system or not base.exists():
        base_dir = LAB / 'runs/qemu-dev/base'
        templates = list(base_dir.glob('template-%s-release-minimal-*.img' % source))
        if source == 'pin' and not templates:   # names from before the switch
            templates = list(base_dir.glob('template-release-minimal-*.img')) or \
                [f for f in base_dir.glob('template-release-*.img') if 'wayland' not in f.name]
        templates.sort(key=lambda f: f.stat().st_mtime)
        if not templates:
            raise SystemExit('no qemu-dev template for %s; run tools/qemu_desktop.sh once' % source)
        subprocess.run(['cp', '--reflink=auto', str(templates[-1]), str(base)], check=True)
        (RUNS / 'qemu' / (stem + '.txt')).write_text(str(templates[-1]) + '\n')
    system = out / 'system.img'
    system.unlink(missing_ok=True)
    subprocess.run(['cp', '--reflink=auto', str(base), str(system)], check=True)
    os.chmod(system, 0o644)

    stage = out / 'stage'
    subprocess.run(['rm', '-rf', str(stage)], check=True)
    (stage / 't').mkdir(parents=True)
    (stage / 'hb-guest').write_text(GUEST)
    run = RUN_HEAD + ''.join('run %s %d\n' % (t[0], t[2]) for t in tests)
    if a.extra_script:
        (stage / 'extra.sh').write_text(a.extra_script.read_text())
        run += 'echo HBX_BEGIN; sh /usr/share/hb/extra.sh 2>&1; echo HBX_END\n'
    (stage / 'run.sh').write_text(run)
    files = {'/usr/bin/hb-guest': (stage / 'hb-guest', True), '/usr/share/hb/run.sh': (stage / 'run.sh', True)}
    if a.extra_script:
        files['/usr/share/hb/extra.sh'] = (stage / 'extra.sh', True)
    for t in tests:
        (stage / 't' / (t[0] + '.sh')).write_text(t[3] + '\n')
        files['/usr/share/hb/t/%s.sh' % t[0]] = (stage / 't' / (t[0] + '.sh'), True)
    links = []
    for overlay in a.overlay:
        for dirpath, dirs, names in os.walk(overlay):
            for n in names + [d for d in dirs if (Path(dirpath) / d).is_symlink()]:
                src = Path(dirpath) / n
                dest = '/' + str(src.relative_to(overlay))
                if src.is_symlink():
                    links.append((dest, os.readlink(src)))
                else:
                    files[dest] = (src.resolve(), os.access(src, os.X_OK) or n.endswith('.dylib'))
    import qd_image
    qd_image.write_root(system, files)
    if links:
        offset, _ = qd_image.partitions(system)[1]
        cmds, made = [], set()
        for dest, target in links:
            for parent in reversed(Path(dest).parents):
                if str(parent) != '/' and str(parent) not in made:
                    made.add(str(parent))
                    cmds.append('mkdir ' + str(parent))       # refused, harmlessly, when it exists
            cmds += ['rm ' + dest, 'symlink %s %s' % (dest, target)]
        subprocess.run(['debugfs', '-w', '-f', '-', '%s?offset=%d' % (system, offset)], input='\n'.join(cmds) + '\n',
                       capture_output=True, text=True, check=True)

    timeout = a.timeout or sum(t[2] for t in tests) + 300
    cmd = [sys.executable, '-B', str(QD / 'qd_run.py'), '--image', str(system), '--out', str(out), '--command', 'sh /usr/bin/hb-guest',
           '--begin', 'HB_BEGIN', '--up', 'HB_BEGIN', '--end', 'HB_END', '--passed', 'HB_END', '--shot-every', '0', '--settle', '0',
           '--timeout', str(timeout), '--memory', str(a.memory), '--smp', str(a.smp),
           '--qemu-arg=-drive', '--qemu-arg=if=none,id=hb,file=%s,format=raw,cache=unsafe' % prefix_img,
           '--qemu-arg=-device', '--qemu-arg=virtio-blk-pci,drive=hb']
    r = subprocess.run(cmd, capture_output=True, text=True)
    (out / 'runner.log').write_text(r.stdout + r.stderr)

    log = (out / 'serial.log').read_bytes().decode('utf-8', 'replace').replace('\r', '') if (out / 'serial.log').exists() else ''
    results = []
    for t in tests:
        name, formula, secs, script, expect, kind = t
        m = re.search(r'HBT_BEGIN %s\n(.*?)(?:HBT_END %s rc=(\d+)|\Z)' % (re.escape(name), re.escape(name)), log, re.S)
        if not m:
            results.append(dict(test=name, formula=formula, kind=kind, status='NOT RUN', why='the guest did not reach it', output=''))
            continue
        body, rc = m.group(1), (int(m.group(2)) if m.group(2) is not None else None)
        timed_out = ('HBT_TIMEOUT ' + name) in body
        body = body.replace('HBT_TIMEOUT %s\n' % name, '')
        ok = rc == 0 and re.search(expect, body, re.M) is not None
        cm = re.search(r'HBT_CRASH_BEGIN %s\n(.*?)HBT_CRASH_END %s' % (re.escape(name), re.escape(name)), log, re.S)
        crash = [l[len('hbcrash: '):] for l in cm.group(1).splitlines() if l.startswith('hbcrash: ')] if cm else []
        results.append(dict(test=name, formula=formula, kind=kind, status='PASS' if ok else 'FAIL', rc=rc,
                            why='' if ok else classify(rc, body, timed_out) + (' at ' + ' < '.join(' '.join(c.split()[2:4]) for c in crash[1:6]) if crash else ''),
                            crash=crash, output=body.strip()[-1500:]))
    finished = 'HB_END' in log
    panic = re.search(r'panic\(cpu[^\n]*\n[^\n]*', log)
    summary = dict(run=a.name, prefix=a.prefix, overlays=[str(o) for o in a.overlay], finished=finished, panic=panic.group(0) if panic else None,
                   passed=sum(r['status'] == 'PASS' for r in results), failed=sum(r['status'] == 'FAIL' for r in results),
                   not_run=sum(r['status'] == 'NOT RUN' for r in results), results=results)
    (out / 'results.json').write_text(json.dumps(summary, indent=1) + '\n')
    lines = ['# Run `%s`: prefix `%s`, overlays %s' % (a.name, a.prefix, ', '.join('`%s`' % o for o in a.overlay) or 'none'), '',
             'Executed in QEMU (arm64 virt, TCG). %d passed, %d failed, %d not run.%s' % (summary['passed'], summary['failed'], summary['not_run'],
                                                                                         '' if finished else ' **The guest did not finish.**'), '',
             '| Test | Formula | Kind | Result | Failure | Output (tail) |', '|---|---|---|---|---|---|']
    for r in results:
        tail = ' '.join(r['output'].split())[-160:].replace('|', '\\|')
        lines.append('| %s | %s | %s | %s | %s | `%s` |' % (r['test'], r['formula'], r['kind'], r['status'], r['why'].replace('|', '\\|'), tail))
    (out / 'results.md').write_text('\n'.join(lines) + '\n')
    print('%s: %d passed, %d failed, %d not run%s  (%s)' % (a.name, summary['passed'], summary['failed'], summary['not_run'],
                                                          '' if finished else ', guest did not finish', out / 'results.md'))
    for r in results:
        if r['status'] != 'PASS':
            print('  %-14s %-8s %s' % (r['test'], r['status'], r['why'][:200]))
    return 0 if finished else 2


if __name__ == '__main__':
    sys.exit(main())
