#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Replace /usr/lib/dyld of a running SiliconForest by a new build, on the target.

  python3 sf-dyld-install.py <new file> <its sha256> <arm64e probe> [program ...]

The dynamic linker is the one file no DYLD_* variable can stand in for: the kernel starts the
linker a program names in its header. So the new file is written beside the installed one
under a name of the same length (/usr/lib/dyl2), and what is run with it are copies of real
programs in which the 13 bytes "/usr/lib/dyld" read "/usr/lib/dyl2" (nothing else changed; a
copy lies beside its original and is started under the original's name, so it finds what the
original finds).

  1. the new file has the SHA-256 given, hashed before anything maps it;
  2. with the new linker (through such copies): the probe, which the linker in use fails (an
     arm64e program with arm64 libraries), ends with status 0; ls, sh and every <program>, run as
     `<program> --version`, end with the status they have with the linker in use;
  3. only then: the linker in use gets the second name /usr/lib/dyld.old (a hard link),
     the new one takes the name by rename(2), sync; the probe, ls, sh and the programs are
     started as they are: the same statuses. If one differs the old file is put back at once.
     This process does both renames itself (link(2), rename(2)): no program has to be started
     to put the old linker back.

A failure before 3 changes nothing. Lines start with SFDYLD; status 0 = installed (or it was).
Way back by hand: ln /usr/lib/dyld.old /usr/lib/dyld.back && mv -f /usr/lib/dyld.back /usr/lib/dyld
(with a broken linker no command starts: that is why step 3 undoes itself)."""
import hashlib, os, subprocess, sys

DYLD = '/usr/lib/dyld'
OLD_NAME, MAGICS = b'/usr/lib/dyld\0', (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf')


def say(text):
    print('SFDYLD ' + text, flush=True)


def run(argv, executable=None):
    try:
        return subprocess.run(argv, executable=executable, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(argv, 124, b'timed out')
    except OSError as e:
        return subprocess.CompletedProcess(argv, 127, str(e).encode())


def last_line(out):
    lines = out.decode('utf-8', 'replace').strip().splitlines()
    return lines[-1][:160] if lines else ''


def with_new_linker(argv, new_name):
    """Run argv[0] as a copy that names the new linker; None if it is not a Mach-O program."""
    real = os.path.realpath(argv[0])
    data = open(real, 'rb').read()
    if data[:4] not in MAGICS or OLD_NAME not in data:
        return None
    copy = real + '.sfdyld'
    with open(copy, 'wb') as f:
        f.write(data.replace(OLD_NAME, new_name))
    os.chmod(copy, 0o755)
    try:
        # the copy is what is executed; the name the program sees is its own (a multi-call
        # binary picks its applet by that name)
        return run(argv, executable=copy)
    finally:
        os.unlink(copy)


def main():
    if len(sys.argv) < 4:
        say('usage: <new file beside /usr/lib/dyld, a name as long> <sha256> <arm64e probe> [program ...]')
        return 2
    new, want, probe, programs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    new_name = new.encode() + b'\0'
    if os.path.dirname(new) != os.path.dirname(DYLD) or len(new) != len(DYLD):
        say('the new file must lie in /usr/lib under a name as long as "dyld"')
        return 2
    tests = [['/bin/ls', '/'], ['/bin/sh', '-c', 'echo x | cat']] + [[p, '--version'] for p in programs if os.access(p, os.X_OK)]

    if run([probe]).returncode == 0:
        say('already installed: the probe runs with %s as it is (%d bytes)' % (DYLD, os.path.getsize(DYLD)))
        if os.path.exists(new):
            os.unlink(new)
        return 0
    got = hashlib.sha256(open(new, 'rb').read()).hexdigest()
    if got != want:
        say('FAIL %s is not the file expected (sha256 %s): nothing was changed' % (new, got))
        os.unlink(new)
        return 1
    say('1 the new file has the sha256 expected: ' + want)
    os.chown(new, 0, 0)
    os.chmod(new, 0o755)

    def fail(text):
        say('FAIL %s: nothing was changed' % text)
        os.unlink(new)
        return 1

    was = run([probe])
    say('2 the probe with the linker in use: status %d (%s)' % (was.returncode, last_line(was.stdout)))
    r = with_new_linker([probe], new_name)
    if r is None or r.returncode != 0:
        return fail('the probe did not run with the new linker (status %s: %s)' % (r.returncode if r else 'not a program', last_line(r.stdout) if r else ''))
    say('2 the probe with the new linker: status 0 (%s)' % last_line(r.stdout))
    expected = []
    for argv in tests:
        before = run(argv)
        r = with_new_linker(argv, new_name)
        if r is None:
            say('2 %s is not a Mach-O program naming %s: not tried' % (argv[0], DYLD))
            continue
        say('2 with the new linker: %s status %d (with the linker in use: %d)' % (argv[0], r.returncode, before.returncode))
        if r.returncode != before.returncode or (argv[0] in ('/bin/ls', '/bin/sh') and (r.returncode != 0 or r.stdout != before.stdout)):
            return fail('%s does not run with the new linker as it does with the one in use (%s)' % (argv[0], last_line(r.stdout)))
        expected.append((argv, before.returncode))

    keep, n = DYLD + '.old', 2
    while os.path.lexists(keep):
        keep, n = DYLD + '.old%d' % n, n + 1
    old_inode, new_inode = os.stat(DYLD).st_ino, os.stat(new).st_ino
    os.link(DYLD, keep)
    if os.stat(keep).st_ino != old_inode:
        os.unlink(keep)
        return fail('could not keep the linker in use as ' + keep)
    os.sync()
    os.rename(new, DYLD)
    os.sync()

    # from here on a new process gets the new linker
    wrong = []
    r = run([probe])
    if r.returncode != 0:
        wrong.append('the probe: status %d (%s)' % (r.returncode, last_line(r.stdout)))
    for argv, status in expected:
        r = run(argv)
        if r.returncode != status:
            wrong.append('%s: status %d, was %d (%s)' % (argv[0], r.returncode, status, last_line(r.stdout)))
    if os.stat(DYLD).st_ino != new_inode:
        wrong.append('%s is not the verified file' % DYLD)
    if wrong:
        back = DYLD + '.sf-back'
        os.link(keep, back)
        os.rename(back, DYLD)
        os.sync()
        say('FAIL the confirmation failed (%s); the old linker was put back: %s is inode %d again' % ('; '.join(wrong), DYLD, os.stat(DYLD).st_ino))
        return 1
    say('3 installed: %s is now inode %d (the verified file), the linker of before is %s (inode %d); new processes: the probe, ls, sh and %d programs as before'
        % (DYLD, new_inode, keep, old_inode, len(expected) - 2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
