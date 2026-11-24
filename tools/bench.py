#!/usr/bin/env python3
"""The shared hardware bench (one M1 + its M5 controller): a first-come-first-served,
re-entrant queue on top of the flock that older callers take on runs/bench.lock.

  tools/bench.sh run <command...>   wait for the bench in arrival order, run the command, release
  tools/bench.sh status             who holds the bench, who waits, and for how long
  tools/bench.sh waiting            number of callers waiting (new and old style); for scripts
  tools/bench.sh held               exit 0 if this process is already inside the bench

Re-entrant: if an ancestor of the caller already holds runs/bench.lock (through this tool or
through a plain `flock runs/bench.lock ...` wrapper), or BENCH_OWNER names the live holder,
`run` starts the command at once. A script may therefore take the bench itself and still be
wrapped by a caller that already holds it.

Fair: each waiter drops a ticket in runs/bench.queue/ named by arrival time and blocks on the
same flock as old-style callers. Whichever ticket holder the kernel wakes first lets every
older ticket run before itself (under its lock), so tickets are served strictly in arrival
order. Old-style `flock` callers still exclude and are excluded, and `status` lists them from
/proc/locks; between them and tickets the kernel's own wake-up order applies.

Set BENCH_WHO to a short label (agent or task name) for the status display.
"""
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
RUNS = Path(os.environ.get('BENCH_DIR') or ROOT / 'runs')  # BENCH_DIR: another directory, for tests.
LOCK = RUNS / 'bench.lock'
QUEUE = RUNS / 'bench.queue'
HOLDER = RUNS / 'bench.holder.json'
LOG = RUNS / 'bench.log'
# Chained use (tools/pcie_iterate.sh PCIE_CHAIN=1). A command that leaves the M1 outside Linux
# writes the shell command that brings it back into the file named by $BENCH_FINISHER. When
# the command ends with that file present, a detached keeper keeps the lock: it hands the
# bench straight to the oldest waiter if that waiter can take the M1 over as it is (ticket made
# with BENCH_CHAIN=1, no old-style waiter that arrived before it), otherwise runs the finisher,
# and only then releases the lock. The caller gets its result without waiting for either.
FINISHER = RUNS / 'bench.finisher'
GRANT = RUNS / 'bench.grant.json'
KEEPER_LOG = RUNS / 'bench.keeper.log'
MAX_CHAIN = int(os.environ.get('BENCH_MAX_CHAIN', '8'))


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def parent(pid):
    try:
        text = Path(f'/proc/{pid}/stat').read_text()
        return int(text.rsplit(')', 1)[1].split()[1])
    except (OSError, ValueError, IndexError):
        return 0


def ancestors(pid=None):
    pid, seen = pid or os.getpid(), []
    while pid > 1 and pid not in seen:
        seen.append(pid)
        pid = parent(pid)
    return seen


def started(pid):
    """Wall-clock start time of a process."""
    try:
        ticks = int(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19])
        boot = next(float(line.split()[1]) for line in Path('/proc/stat').read_text().splitlines()
                    if line.startswith('btime'))
        return boot + ticks / os.sysconf('SC_CLK_TCK')
    except (OSError, ValueError, IndexError, StopIteration):
        return None


def command_of(pid):
    try:
        return Path(f'/proc/{pid}/cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace').strip()
    except OSError:
        return '?'


def has_lock_open(pid):
    """True if the process has runs/bench.lock open (None if that cannot be read)."""
    try:
        target = os.stat(LOCK)
        for fd in os.listdir(f'/proc/{pid}/fd'):
            try:
                info = os.stat(f'/proc/{pid}/fd/{fd}')
            except OSError:
                continue
            if (info.st_dev, info.st_ino) == (target.st_dev, target.st_ino):
                return True
        return False
    except OSError:
        return None


def flock_table():
    """(holders, waiters): pids holding / blocked on the bench flock, from /proc/locks."""
    holders, waiters = [], []
    try:
        inode = os.stat(LOCK).st_ino
        lines = Path('/proc/locks').read_text().splitlines()
    except OSError:
        return holders, waiters
    for line in lines:
        fields = line.split()
        blocked = '->' in fields
        if blocked:
            fields.remove('->')
        if len(fields) < 6 or fields[1] != 'FLOCK':
            continue
        try:
            pid, node = int(fields[4]), int(fields[5].rsplit(':', 1)[1])
        except ValueError:
            continue
        # Inode numbers alone can collide across filesystems: confirm through the open file.
        if node != inode or has_lock_open(pid) is False:
            continue
        (waiters if blocked else holders).append(pid)
    return holders, waiters


def owned():
    """The pid through which this process already holds the bench, or None."""
    holders, _ = flock_table()
    mine = ancestors()
    for pid in holders:
        if pid in mine:
            return pid
    claimed = os.environ.get('BENCH_OWNER', '')
    if claimed.isdigit() and int(claimed) in holders and alive(int(claimed)):
        return int(claimed)
    return None


def tickets():
    """Live tickets, oldest first; tickets of dead processes are removed."""
    result = []
    try:
        names = sorted(os.listdir(QUEUE))
    except OSError:
        return result
    for name in names:
        try:
            pid = int(name.rsplit('-', 1)[1])
        except (ValueError, IndexError):
            continue
        path = QUEUE / name
        if not alive(pid):
            try:
                path.unlink()
            except OSError:
                pass
            continue
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            data = {}
        result.append((name, pid, data))
    return result


def who():
    return os.environ.get('BENCH_WHO') or os.environ.get('USER', '?')


def describe_holder():
    holders, _ = flock_table()
    if not holders:
        return None
    pid = holders[0]
    try:
        data = json.loads(HOLDER.read_text())
    except (OSError, ValueError):
        data = {}
    if data.get('pid') == pid:
        return dict(pid=pid, who=data.get('who', '?'), command=data.get('command', '?'),
                    since=data.get('since'), style='bench.sh')
    return dict(pid=pid, who='(flock)', command=command_of(pid), since=started(pid), style='flock')


def age(since):
    if not since:
        return '?'
    seconds = int(time.time() - since)
    return f'{seconds // 60}m{seconds % 60:02d}s'


def status():
    holder = describe_holder()
    if holder:
        print(f"held {age(holder['since'])} by {holder['who']} pid {holder['pid']} [{holder['style']}]: "
              f"{holder['command'][:150]}")
    else:
        print('free')
    rows = [(data.get('since') or 0, f"{data.get('who', '?')} pid {pid} [bench.sh]: {data.get('command', '?')[:150]}")
            for _, pid, data in tickets()]
    _, blocked = flock_table()
    ticket_pids = {pid for _, pid, _ in tickets()}
    rows += [(started(pid) or 0, f'(flock) pid {pid} [flock]: {command_of(pid)[:150]}')
             for pid in blocked if pid not in ticket_pids]
    for position, (since, text) in enumerate(sorted(rows), 1):
        print(f'  {position}. waiting {age(since)}: {text}')
    if not rows:
        print('  nobody waiting')
    return 0


def waiting():
    _, blocked = flock_table()
    ticket_pids = {pid for _, pid, _ in tickets()}
    print(len(ticket_pids) + len([pid for pid in blocked if pid not in ticket_pids and pid != os.getpid()]))
    return 0


def read_grant():
    try:
        return json.loads(GRANT.read_text())
    except (OSError, ValueError):
        return None


def next_chainable():
    """The head ticket, if it may take the bench over without the M1 returning to Linux."""
    queue = tickets()
    if not queue or not queue[0][2].get('chain'):
        return None
    name, pid, data = queue[0]
    _, blocked = flock_table()
    ticket_pids = {p for _, p, _ in queue}
    for other in blocked:
        if other not in ticket_pids and (started(other) or 0) < (data.get('since') or 0):
            return None  # An old-style caller has waited longer: it goes first, from Linux.
    return name, pid, data


def keeper(fd, label):
    """Detached: holds the lock until the M1 is back in Linux (or handed to the next waiter)."""
    os.setsid()
    out = os.open(KEEPER_LOG, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
    null = os.open(os.devnull, os.O_RDONLY)
    os.dup2(null, 0), os.dup2(out, 1), os.dup2(out, 2)
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    chained = 0
    try:
        while FINISHER.exists():
            HOLDER.write_text(json.dumps(dict(pid=os.getpid(), who='keeper after ' + label, since=time.time(),
                                              command='M1 outside Linux: handing over or returning it')))
            head = next_chainable() if chained < MAX_CHAIN else None
            if head is not None:
                name, pid, data = head
                GRANT.write_text(json.dumps(dict(ticket=name, pid=pid, keeper=os.getpid(), since=time.time())))
                chained += 1
                log('handed_over', to=data.get('who'), pid=pid, chained=chained)
                HOLDER.write_text(json.dumps(dict(pid=os.getpid(), who=str(data.get('who')) + ' (chained)',
                                                  since=time.time(), command=data.get('command', '?'))))
                while GRANT.exists() and alive(pid):
                    time.sleep(0.25)
                GRANT.unlink(missing_ok=True)
                continue
            command = FINISHER.read_text().strip()
            print(time.strftime('%F %T'), 'finisher:', command, flush=True)
            code = subprocess.run(['bash', '-c', command], stdin=subprocess.DEVNULL, stdout=out, stderr=out).returncode
            log('finisher', status=code, command=command[:200])
            if code == 0:
                FINISHER.unlink(missing_ok=True)
            else:
                FINISHER.replace(FINISHER.with_name('bench.finisher.failed'))
                print(time.strftime('%F %T'), 'FINISHER FAILED: the M1 may not be in Linux', flush=True)
    finally:
        try:
            if json.loads(HOLDER.read_text()).get('pid') == os.getpid():
                HOLDER.unlink()
        except (OSError, ValueError):
            pass
        log('released', who='keeper after ' + label, pid=os.getpid())
        os.close(fd)
        os._exit(0)


def log(event, **fields):
    try:
        with LOG.open('a') as out:
            out.write(json.dumps(dict(time=round(time.time(), 1), event=event, **fields)) + '\n')
    except OSError:
        pass


def run(argv):
    if not argv:
        print('usage: bench.sh run <command...>', file=sys.stderr)
        return 2
    owner = owned()
    if owner is not None:
        # Already inside the bench (our own holder or a plain flock wrapper above us).
        os.environ['BENCH_OWNER'] = str(owner)
        os.execvp(argv[0], argv)
    # Two processes: this one is what the caller waits for; the worker queues, holds the lock,
    # runs the command and reports its status here. The worker may outlive us as the keeper,
    # so the lock is in the name of a process that is alive for as long as it is held.
    sys.stdout.flush(), sys.stderr.flush()
    reader, writer = os.pipe()
    worker = os.fork()
    if worker == 0:
        os.close(reader)
        code = 1
        try:
            code = hold_and_run(argv, writer)
        finally:
            os._exit(code)
    os.close(writer)

    def forward(signum, frame):
        try:
            os.kill(worker, signum)
        except ProcessLookupError:
            pass

    signal.signal(signal.SIGTERM, forward)
    signal.signal(signal.SIGHUP, forward)
    signal.signal(signal.SIGINT, signal.SIG_IGN)  # The terminal delivers it to the worker and the command too.
    data = b''
    while True:
        try:
            block = os.read(reader, 64)
        except InterruptedError:
            continue
        if not block:
            break
        data += block
    try:
        return int(data.decode().strip())
    except ValueError:
        return 1  # The worker died without reporting.


class Granted(Exception):
    """Raised in the waiting worker by SIGUSR1: the lock holder handed the bench to us."""


def run_finisher():
    command = FINISHER.read_text().strip()
    print('bench: returning the M1 to Linux first:', command, file=sys.stderr, flush=True)
    code = subprocess.run(['bash', '-c', command], stdin=subprocess.DEVNULL).returncode
    log('finisher', status=code, command=command[:200])
    if code == 0:
        FINISHER.unlink(missing_ok=True)
    else:
        FINISHER.replace(FINISHER.with_name('bench.finisher.failed'))
    return code


def hold_and_run(argv, report):
    """Worker: queue, take the bench (or a hand-over), run the command, report, keep if needed."""
    QUEUE.mkdir(exist_ok=True)
    arrived = time.time()
    label, text = who(), ' '.join(argv)
    ticket = QUEUE / f'{time.time_ns():020d}-{os.getpid()}'
    record = dict(pid=os.getpid(), who=label, command=text, since=arrived,
                  chain=os.environ.get('BENCH_CHAIN') == '1')
    ticket.write_text(json.dumps(record))
    fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o644)
    child, granted, code, reported = None, None, 130, False

    def forward(signum, frame):
        if child is not None and child.poll() is None:
            child.send_signal(signum)
        else:
            raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, forward)
    signal.signal(signal.SIGHUP, forward)

    def on_grant(signum, frame):
        raise Granted()

    watching = threading.Event()

    def watch():
        # Beside the blocking flock: a hand-over addressed to us, and the waiting notice.
        told = 0.0
        while not watching.wait(0.25):
            grant = read_grant()
            if grant and grant.get('pid') == os.getpid() and grant.get('keeper') in flock_table()[0]:
                os.kill(os.getpid(), signal.SIGUSR1)
                return
            if time.time() - told >= 60:
                told = time.time()
                queue = tickets()
                ahead = [data.get('who', '?') for name, _, data in queue if name < ticket.name]
                holder = describe_holder()
                print(f"bench: waiting {age(arrived)}; held by "
                      f"{(holder['who'] + ' for ' + age(holder['since'])) if holder else 'nobody'}"
                      f"{'; ahead of us: ' + ', '.join(ahead) if ahead else ''}", file=sys.stderr, flush=True)

    try:
        try:
            signal.signal(signal.SIGUSR1, on_grant)
            threading.Thread(target=watch, daemon=True).start()
            # Block in the kernel's queue for the lock, exactly as old-style callers do, so that
            # neither kind overtakes the other; the order among tickets is settled below.
            fcntl.flock(fd, fcntl.LOCK_EX)
        except Granted:
            granted = (read_grant() or {}).get('keeper')
            if granted is None:
                raise KeyboardInterrupt
        finally:
            watching.set()
            signal.signal(signal.SIGUSR1, signal.SIG_IGN)
        if granted is None:
            HOLDER.write_text(json.dumps(dict(pid=os.getpid(), who=label, command=text, since=time.time())))
            # First come first served among tickets: whoever the kernel woke lets every older
            # waiter run first, under this lock.
            while True:
                older = [entry for entry in tickets() if entry[0] < ticket.name]
                if not older:
                    break
                name, pid, data = older[0]
                if FINISHER.exists() and not data.get('chain'):
                    run_finisher()
                GRANT.write_text(json.dumps(dict(ticket=name, pid=pid, keeper=os.getpid(), since=time.time())))
                log('served_first', who=data.get('who'), pid=pid, by=label)
                HOLDER.write_text(json.dumps(dict(pid=os.getpid(), who=str(data.get('who')) + ' (ahead of ' + label + ')',
                                                  since=time.time(), command=data.get('command', '?'))))
                while GRANT.exists() and alive(pid):
                    time.sleep(0.25)
                GRANT.unlink(missing_ok=True)
        acquired = time.time()
        if granted is None:
            HOLDER.write_text(json.dumps(dict(pid=os.getpid(), who=label, command=text, since=acquired)))
        ticket.unlink(missing_ok=True)
        log('acquired', who=label, pid=os.getpid(), waited=round(acquired - arrived, 1), command=text[:200],
            chained=granted is not None)
        if granted is None and FINISHER.exists() and os.environ.get('BENCH_CHAIN') != '1':
            run_finisher()  # The M1 was left outside Linux: bring it back before an ordinary command.
        env = dict(os.environ, BENCH_OWNER=str(granted or os.getpid()), BENCH_FINISHER=str(FINISHER),
                   BENCH_WAITED=str(round(acquired - arrived, 1)))
        child = subprocess.Popen(argv, env=env)
        while True:
            try:
                code = child.wait()
                break
            except KeyboardInterrupt:
                continue  # The terminal sent SIGINT to the command as well; wait for it.
        code = code if code >= 0 else 128 - code
        log('finished', who=label, pid=os.getpid(), held=round(time.time() - acquired, 1), status=code)
        os.write(report, str(code).encode())
        os.close(report)
        reported = True
        if granted is None and FINISHER.exists():
            keeper(fd, label)  # Does not return; releases the lock when the M1 is back in Linux.
        return code
    except KeyboardInterrupt:
        return 130
    finally:
        ticket.unlink(missing_ok=True)
        if granted is not None:
            GRANT.unlink(missing_ok=True)  # Tells the keeper this chained turn is over.
        try:
            if json.loads(HOLDER.read_text()).get('pid') == os.getpid():
                HOLDER.unlink()
        except (OSError, ValueError):
            pass
        if not reported:
            try:
                os.write(report, str(code).encode())
            except OSError:
                pass
        os.close(fd)


def main(argv):
    if len(argv) >= 1 and argv[0] == 'run':
        return run(argv[1:])
    if argv == ['status']:
        return status()
    if argv == ['waiting']:
        return waiting()
    if argv == ['held']:
        return 0 if owned() is not None else 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
