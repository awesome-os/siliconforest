#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Run on the target (/opt/homebrew/bin/python3): times between input and what the WindowServer hands
# the compositor, from the WindowServer's SF_WS_TIMING lines (wall clock, ms). A commit is on the
# screen when the compositor releases the buffer before it (it releases a buffer once a frame no
# longer samples it), so "release" after a commit is the time the new frame was rendered.
#
#   sf-latency.py launch [VAR=value ...]   start Terminal (the old one is ended), report ms from exec to
#                                          its login shell and to its first commit (window with prompt)
#   sf-latency.py output N                 write N lines to the newest Terminal shell's tty (bypasses the
#                                          shell: the path is pty -> Terminal -> AppKit -> WindowServer);
#                                          ms to the next commit and to the release after it
#   sf-latency.py inject '<request;...>'   hand the compositor's inject module a request (test build only)
#                                          and report ms from the moment it was taken to the next commit
#                                          of each surface
#   sf-latency.py trace '<request;...>' [settle ms]   every commit of each surface from the request to settle ms
#                                          after it: count, sizes, gaps between commits (drags, wheel, floods)
#   sf-latency.py ctrlc PROGRAM            Control-C injected while PROGRAM runs in Terminal: ms until it is gone
# The Terminal started by `launch` logs to /private/tmp/sf-terminal/launch.log; output and inject read it.
import os, subprocess, sys, time

LOG = '/private/tmp/sf-terminal/launch.log'
INJECT = '/private/var/run/gpu-desktop/inject'


def now():
    return time.time() * 1000


def events(since):
    out = []
    for line in open(LOG, errors='replace'):
        p = line.split()
        if len(p) > 3 and p[0] == 'WSTIME' and float(p[1]) > since:
            out.append((float(p[1]), p[2], p[3]))
    return out


def launch(args):
    env = dict(XDG_RUNTIME_DIR='/private/var/run/gpu-desktop', WAYLAND_DISPLAY='wayland-0', HOME='/var/root',
               SF_WS_TIMING='1', PATH='/usr/bin:/bin:/usr/sbin:/sbin')
    env.update(a.split('=', 1) for a in args)
    subprocess.run(['pkill', '-x', 'Terminal'])
    time.sleep(1.5)
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    log = open(LOG, 'w')
    t0 = now()
    # SFLAT_CATCH=<sf-catch>: run under the Mach exception reporter (a crash is reported in the log)
    catch = [os.environ['SFLAT_CATCH']] if os.environ.get('SFLAT_CATCH') else []
    subprocess.Popen(catch + ['/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal'], env=env, stdout=log,
                     stderr=subprocess.STDOUT, cwd=os.path.dirname(LOG))
    shell = None
    while now() - t0 < 12000:
        if shell is None:
            ps = subprocess.run(['ps', '-ax', '-o', 'ppid=,command='], capture_output=True, text=True).stdout
            if any(l.split()[1:2] == ['-zsh'] for l in ps.splitlines() if l.split()):
                shell = now() - t0
        time.sleep(0.05)
    commits = [t - t0 for t, what, _ in events(0) if what == 'commit']
    print('LAUNCH shell=%s first_commit=%s commits=%d' % ('%.0f' % shell if shell else 'none',
          '%.0f' % commits[0] if commits else 'none', len(commits)))


def output(n):
    ps = subprocess.run(['ps', '-ax', '-o', 'pid=,tty=,command='], capture_output=True, text=True).stdout
    ttys = [l.split()[1] for l in ps.splitlines() if l.split()[2:3] == ['-zsh'] and l.split()[1].startswith('ttys')]
    text = ''.join('line %03d %s\r\n' % (i, 'abcdefghijklmnopqrstuvwxyz0123456789' * 3) for i in range(n))
    fd = os.open('/dev/' + ttys[-1], os.O_WRONLY | os.O_NOCTTY)
    t0 = now()
    os.write(fd, text.encode())
    time.sleep(1)
    ev = events(t0)
    commit = next((t for t, what, _ in ev if what == 'commit'), None)
    release = next((t for t, what, _ in ev if what == 'release' and commit and t > commit), None)
    print('OUTPUT lines=%d commit=+%s release=+%s' % (n, '%.0f' % (commit - t0) if commit else 'none',
          '%.0f' % (release - t0) if release else 'none'))


def inject(request):
    for name in ('done', 'request.taken'):
        try:
            os.remove(os.path.join(INJECT, name))
        except OSError:
            pass
    with open(os.path.join(INJECT, 'request.part'), 'w') as f:
        f.write(request.replace(';', '\n') + '\n')
    os.rename(os.path.join(INJECT, 'request.part'), os.path.join(INJECT, 'request'))
    while not os.path.exists(os.path.join(INJECT, 'request.taken')):
        time.sleep(0.0005)
    t0 = now()
    while not os.path.exists(os.path.join(INJECT, 'done')):
        time.sleep(0.01)
    time.sleep(0.2)
    first = {}
    for t, what, surface in events(t0):
        if what.startswith('commit') and surface not in first:
            first[surface] = t - t0
    print('INJECT ' + ' '.join('%s:+%.0f' % (s, ms) for s, ms in first.items()))


def trace(request, settle_ms):
    """Every commit and release from the moment the request was taken until settle_ms after it was done,
    then the gaps between successive commits of the busiest surface (a drag, scroll or flood)."""
    for name in ('done', 'request.taken'):
        try:
            os.remove(os.path.join(INJECT, name))
        except OSError:
            pass
    with open(os.path.join(INJECT, 'request.part'), 'w') as f:
        f.write(request.replace(';', '\n') + '\n')
    os.rename(os.path.join(INJECT, 'request.part'), os.path.join(INJECT, 'request'))
    while not os.path.exists(os.path.join(INJECT, 'request.taken')):
        time.sleep(0.0005)
    t0 = now()
    while not os.path.exists(os.path.join(INJECT, 'done')):
        time.sleep(0.01)
    done = now() - t0
    time.sleep(settle_ms / 1000.0)
    commits = {}
    for line in open(LOG, errors='replace'):
        p = line.split()
        if len(p) > 3 and p[0] == 'WSTIME' and float(p[1]) > t0 and p[2] == 'commit':
            f = dict(x.split('=', 1) for x in p[3:] if '=' in x)
            commits.setdefault(f.get('surface'), []).append((float(p[1]) - t0, f.get('size', '?'), float(f.get('took', 0))))
    print('TRACE done=+%.0f' % done)
    # configures from the compositor (SF_WS_TIMING): ms from each to the first commit at its size
    confs = []
    for line in open(LOG, errors='replace'):
        p = line.split()
        if len(p) > 3 and p[0] == 'WSTIME' and float(p[1]) > t0 and p[2] == 'configure':
            f = dict(x.split('=', 1) for x in p[3:] if '=' in x)
            confs.append((float(p[1]) - t0, f.get('surface'), f.get('size')))
    if confs:
        lat = []
        for t, surf, size in confs:
            later = [c[0] for c in commits.get(surf, []) if c[0] >= t]
            if later:
                lat.append(later[0] - t)
        lat.sort()
        print('  configures=%d  configure->next commit ms: median=%s p90=%s max=%s; gaps between configures median=%s' % (
            len(confs), '%.0f' % lat[len(lat) // 2] if lat else '-', '%.0f' % lat[int(len(lat) * 0.9)] if lat else '-',
            '%.0f' % lat[-1] if lat else '-',
            '%.0f' % sorted(b[0] - a[0] for a, b in zip(confs, confs[1:]))[len(confs) // 2 - 1] if len(confs) > 2 else '-'))
    for s, c in commits.items():
        gaps = [b[0] - a[0] for a, b in zip(c, c[1:])]
        sizes = []
        for _, size, _ in c:
            if not sizes or sizes[-1] != size:
                sizes.append(size)
        g = sorted(gaps)
        print('  surface=%s commits=%d first=+%.0f last=+%.0f sizes=%s gaps(ms) median=%s p90=%s max=%s' % (
            s, len(c), c[0][0], c[-1][0], ','.join(sizes[:6]) + (',...' if len(sizes) > 6 else ''),
            '%.0f' % g[len(g) // 2] if g else '-', '%.0f' % g[int(len(g) * 0.9)] if g else '-', '%.0f' % g[-1] if g else '-'))
        if os.environ.get('SFLAT_ALL'):
            print('    ' + ' '.join('+%.0f:%s' % (t, size) for t, size, _ in c))


def ctrlc(program):
    """With PROGRAM running in Terminal (yes, find, cat ...): inject Control-C and report ms from the moment
    the request was taken to the moment the process is gone."""
    def running():
        ps = subprocess.run(['ps', '-ax', '-o', 'pid=,command='], capture_output=True, text=True).stdout
        return [l.split()[0] for l in ps.splitlines() if l.split()[1:2] == [program]]
    if not running():
        sys.exit('no %s running' % program)
    for name in ('done', 'request.taken'):
        try:
            os.remove(os.path.join(INJECT, name))
        except OSError:
            pass
    with open(os.path.join(INJECT, 'request.part'), 'w') as f:
        f.write('key e0 down\nkey 06 down\nkey 06 up\nkey e0 up\n')
    os.rename(os.path.join(INJECT, 'request.part'), os.path.join(INJECT, 'request'))
    while not os.path.exists(os.path.join(INJECT, 'request.taken')):
        time.sleep(0.0005)
    t0 = now()
    while running() and now() - t0 < 10000:
        time.sleep(0.002)
    print('CTRLC %s gone=+%.0f' % (program, now() - t0) if not running() else 'CTRLC %s still running after 10 s' % program)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'launch':
        launch(sys.argv[2:])
    elif what == 'output':
        output(int(sys.argv[2]) if len(sys.argv) > 2 else 40)
    elif what == 'inject':
        inject(sys.argv[2])
    elif what == 'ctrlc':
        ctrlc(sys.argv[2] if len(sys.argv) > 2 else 'yes')
    elif what == 'trace':
        trace(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 500)
    else:
        sys.exit(__doc__ or 'launch | output N | inject REQUEST')
