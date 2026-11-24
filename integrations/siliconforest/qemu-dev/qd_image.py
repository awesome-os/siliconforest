#!/usr/bin/env python3
"""Disk images for the QEMU desktop loop (arm64 `virt`, the same binaries as the M1).

Two layers, both under runs/qemu-dev/ (never committed):

  template   the base image of build-image.nix (upstream's arm64-virt-minimal image, GPT: ESP +
             ext4 root, with our kernel collection and loader) with
               * the kernel's IOKitInitializeTime waits shortened (the M1 launcher's patch),
               * our boot arguments,
               * the slow-changing userland the M1 RAM root carries (fonts, xkb data, the
                 patched libSystem, audio server, terminfo, desk picture).
             Rebuilt only when one of those inputs changes (a hash of them names it).
  run image  a reflink copy of the template plus the files that change while working:
             compositor, terminal, session script, any --file extras. About a second.

Nothing is mounted: the ESP is edited with mtools, the root with debugfs, both at the
partition's offset inside the image file.

The SiliconForest source is the one ../rebase/pd.nix selects: upstream's tip, or the old pin
with SF_PD=pin. Remembered Nix answers and templates carry the source's name, so pin and tip
runs neither read each other's store paths nor evict each other's templates.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys

LAB = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = LAB / 'runs/qemu-dev'
SECTOR = 512
NIX = ['nix-build', '--extra-experimental-features', 'nix-command',
       '--extra-experimental-features', 'flakes', '--no-out-link']
# The SiliconForest source every recipe asked here builds from (../rebase/pd.nix): `tip` unless
# SF_PD=pin. It is part of every remembered Nix answer's key and of every template's name.
SOURCE = os.environ.get('SF_PD') or 'tip'
if SOURCE not in ('pin', 'tip'):
    raise SystemExit('SF_PD=%s: expected pin or tip' % SOURCE)
ENV = dict(os.environ, PATH=str(Path.home() / '.nix-profile/bin') + ':' + os.environ['PATH'],
           NIX_CONFIG='build-users-group = ', SF_PD=SOURCE)
# no_lock_timeouts=1: upstream's switch for emulated guests on a busy host, where a descheduled
# vCPU thread otherwise reads as a spinlock deadlock (panic seen here at host load 40).
BOOTARGS = {'debug': 'debug=0x218 -nogzalloc_mode keepsyms=1 serial=3 gopconsole=1 serial_video_mirror=0 -noprogress no_interrupt_masked_debug=1 no_lock_timeouts=1',
            'release': 'serial=3 gopconsole=1 serial_video_mirror=0 -noprogress keepsyms=1 no_lock_timeouts=1'}


def sh(argv, **kw):
    return subprocess.run([str(a) for a in argv], check=True, text=True, capture_output=True, env=ENV, **kw).stdout


REFRESH = False


def nix(directory, *args, source=None):
    """A Nix build's output path. Evaluating a flake costs tens of seconds each time, so the
    answers are remembered in runs/qemu-dev/base/inputs.json until --refresh. A key starts
    with the source's name (`pin: ` or `tip: `); the keys without one are the pin's answers
    from before the source could be chosen and are never read again. `source` asks for one
    source whatever this run's is."""
    source = source or SOURCE
    cache = OUT / 'base/inputs.json'
    known = json.loads(cache.read_text()) if cache.exists() else {}
    key = source + ': ' + ' '.join([str(directory)] + [str(a) for a in args])
    if REFRESH or key not in known or not Path(known[key]).exists():
        out = subprocess.run([str(a) for a in NIX + list(args)], check=True, text=True, capture_output=True,
                             env=dict(ENV, SF_PD=source), cwd=LAB / directory).stdout
        known[key] = out.strip().splitlines()[-1]
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps(known, indent=1) + '\n')
    return known[key]


def digest(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def partitions(image):
    """[(first byte, bytes)] of the GPT partitions, in table order."""
    with open(image, 'rb') as f:
        f.seek(SECTOR)
        header = f.read(92)
        assert header[:8] == b'EFI PART', 'not a GPT image'
        lba, count, size = struct.unpack_from('<QII', header, 72)
        f.seek(lba * SECTOR)
        out = []
        for _ in range(count):
            entry = f.read(size)
            if entry[:16] != bytes(16):
                first, last = struct.unpack_from('<QQ', entry, 32)
                out.append((first * SECTOR, (last - first + 1) * SECTOR))
    return out


def tool(name):
    """mtools from the Nix store (not installed on the host)."""
    found = shutil.which(name)
    if found:
        return found
    expr = '(import %s/integrations/siliconforest/rebase/pd.nix).pkgs.mtools' % LAB
    return str(Path(nix('.', '--impure', '-E', expr), 'bin', name))


TEMPLATE_VERSION = 3    # raise when the template's construction changes


def base_image(kernel, variant='minimal'):
    """Upstream's arm64-virt-minimal image with our kernel collection and loader
    (build-image.nix); built once by Nix. `release` has upstream's RELEASE kernel (several
    times faster under TCG), `debug` the DEBUG kernel."""
    override = os.environ.get('QD_BASE_IMAGE')
    if override:
        return Path(override)
    extra = [] if variant == 'minimal' else ['--argstr', 'variant', variant]
    out = Path(nix('integrations/siliconforest/qemu-dev', 'build-image.nix', '--argstr', 'kernelConfig', kernel, *extra))
    return next(out.glob('*.img'))


def patch_time_waits(kernel, rtc, quiet):
    """As m1-bringup/host_progress.py patch_time_waits: the 30 s wait for an IORTC resource
    nobody publishes and the 30 s registry-quiet wait before the root mount, in the two code
    shapes seen so far."""
    def once(pattern, start=0, end=None):
        end = len(kernel) if end is None else end
        at = kernel.find(pattern, start, end)
        return at if at >= 0 and kernel.find(pattern, at + 4, end) < 0 else None
    out = bytearray(kernel)
    # DEBUG: mov w8,#30; stur w8,[x29,#-8]; stur wzr,[x29,#-4] ... ldr x8,[x8] (a literal {30,0})
    first = once(bytes.fromhex('c8038052' 'a8831fb8' 'bfc31fb8'))
    if first is not None:
        second = once(bytes.fromhex('080140f9' 'e9430091' 'e90700f9' 'e80b00f9'), first, first + 0x80)
        if second is None:
            return kernel, False
        out[first:first + 4] = struct.pack('<I', 0x52800008 | rtc << 5)        # mov w8,#rtc
        out[second:second + 4] = struct.pack('<I', 0xd2800008 | quiet << 5)    # mov x8,#quiet
        return bytes(out), True
    # DEVELOPMENT and RELEASE: mov w19,#30; str x19,[sp,#8] ... str x19,[sp]; bl; mov x1,sp.
    # One register feeds both waits.
    first = once(bytes.fromhex('d3038052' 'f30700f9'))
    if first is not None:
        second = once(bytes.fromhex('f30300f9'), first + 8, first + 0x40)
        if second is None or kernel[second + 8:second + 12] != bytes.fromhex('e1030091'):
            return kernel, False
        out[first:first + 4] = struct.pack('<I', 0x52800013 | max(rtc, quiet) << 5)   # mov w19,#each
        return bytes(out), True
    return kernel, False


def stable_payload(mesa, libsystem='spawnattr'):
    """Destination -> source for the slow-changing files. Mirrors tools/gpu_desktop.sh and the
    M1 RAM root's base mapping, without anything private (firmware) or M1-only (Wi-Fi join)."""
    files = {}
    base = json.loads((LAB / 'artifacts/m1-desktop-010/apcie-port-candidate-003/mapping.json').read_text())['files']
    skip = ('/usr/bin/darwin-desktop-test', '/usr/bin/darwin-apcie-port-observe', '/usr/bin/darwin-trackpad-enum',
            '/usr/bin/chonkstep-darwin')
    for e in base:
        if e['destination'] not in skip and not e['destination'].startswith('/Library/Firmware/'):
            files[e['destination']] = (e['source'], e.get('executable', False))
    cd = LAB / 'integrations/siliconforest/chonkstep-darwin'
    for d in ('/usr/share/terminfo/x/xterm-256color', '/usr/share/terminfo/78/xterm-256color'):
        files[d] = (cd / 'terminfo/xterm-256color', False)
    files['/usr/share/backgrounds/siliconforest/siliconforest.png'] = (cd / 'backgrounds/siliconforest.png', False)
    sound = 'integrations/siliconforest/m1-sound'
    # The audio server, its frameworks, tools and sfplay from this run's source (the tip: our
    # /dev backend on upstream's server, with the SiliconForest names). QD_AUDIO=0 leaves them
    # out (tools/qemu_desktop.sh --no-sound sets it).
    if os.environ.get('QD_AUDIO', '1') != '0':
        a = Path(nix(sound + '/sysaudio', 'build-sysaudio.nix', '-A', 'all'))
        play = Path(nix(sound, 'build-sfplay.nix'))
        for rel in ('usr/sbin/coreaudiod', 'usr/bin/sftone', 'usr/bin/sfvolume'):
            files['/' + rel] = (a / rel, True)
        files['/usr/bin/sfplay'] = (play / 'usr/bin/sfplay', True)
        for f in ('CoreAudio', 'AudioToolbox', 'AudioUnit'):
            rel = 'System/Library/Frameworks/%s.framework/Versions/A/%s' % (f, f)
            files['/' + rel] = (a / rel, True)
    # Network: the lab's DHCP client (m1-bwfm/dhcp) and this directory's qemu-net-up.
    dhcp = LAB / 'runs/m1-desktop-010/sf-netd-build-001/result/usr/bin/sf-dhcp'
    if dhcp.exists():
        files['/usr/bin/sf-dhcp'] = (dhcp, True)
    files['/usr/bin/qemu-net-up'] = (HERE / 'guest/qemu-net-up', True)
    if libsystem == 'homebrew' or SOURCE == 'pin':
        # The lab's default libSystem (../libsystem-default, GitHub issue 46: among other things,
        # an arm64 function with a stack frame over 4 KiB no longer loses its first argument),
        # built from this run's source. A pin run always takes it from here: the base mapping
        # above names the libSystem of the default source, which is the tip's since the switch.
        lib = Path(nix('integrations/siliconforest/homebrew', 'build-libsystem.nix'))
        files['/usr/lib/libSystem.B.dylib'] = (lib / 'usr/lib/libSystem.B.dylib', True)
    if mesa:
        t = Path(nix('integrations/siliconforest/m1-mesa-asahi', 'agx-offscreen-test.nix'))
        for l in ('libEGL.1.dylib', 'libGLESv2.2.dylib', 'libgallium-26.3.0-devel.dylib', 'libdrm.2.dylib'):
            files['/usr/local/lib/mesa-asahi/' + l] = (t / 'usr/local/lib/mesa-asahi' / l, True)
    return {d: (Path(s).resolve(strict=True), x) for d, (s, x) in files.items()}


def quote(value):
    value = str(value)
    assert not any(c in value for c in '\n\r\x00"'), value
    return '"' + value + '"'


def read_root(image, path):
    """A file of the image's ext4 root, as bytes."""
    offset, _ = partitions(image)[1]
    return subprocess.run(['debugfs', '-R', 'cat ' + quote(path), '%s?offset=%d' % (image, offset)],
                          capture_output=True, check=True).stdout


def overlay_entries(overlay):
    """(files, links) of a tree rooted at /: regular files as write_root takes them (a .dylib
    or a file with an execute bit is executable), symbolic links as (path, target)."""
    files, links = {}, []
    overlay = Path(overlay)
    for dirpath, dirs, names in os.walk(overlay):
        for n in names + [d for d in dirs if (Path(dirpath) / d).is_symlink()]:
            src = Path(dirpath) / n
            dest = '/' + str(src.relative_to(overlay))
            if src.is_symlink():
                links.append((dest, os.readlink(src)))
            else:
                files[dest] = (src.resolve(), os.access(src, os.X_OK) or n.endswith('.dylib'))
    return files, links


def write_root(image, files, dirs=(), remove=(), links=()):
    """Add or replace regular files in the image's ext4 root with debugfs (no mount).
    dirs: (path, uid, gid) directories to make; remove: paths to delete; links: (path, target)
    symbolic links to make (replacing what is at the path)."""
    offset, _ = partitions(image)[1]
    target = '%s?offset=%d' % (image, offset)
    existing_dirs, commands = set(), []

    def exists(path):
        r = subprocess.run(['debugfs', '-R', 'stat ' + quote(path), target], capture_output=True, text=True)
        return 'Type:' in r.stdout
    for dest in sorted(list(files) + [l[0] for l in links]):
        for parent in reversed(PurePosixPath(dest).parents):
            p = str(parent)
            if p != '/' and p not in existing_dirs:
                if not exists(p):
                    commands.append('mkdir ' + quote(p))
                existing_dirs.add(p)
    for path in remove:
        commands.append('rm ' + quote(path))
    for path, uid, gid in dirs:
        for parent in list(reversed(PurePosixPath(path).parents)) + [PurePosixPath(path)]:
            if str(parent) != '/' and str(parent) not in existing_dirs:
                if not exists(str(parent)):
                    commands.append('mkdir ' + quote(parent))
                existing_dirs.add(str(parent))
        commands += ['sif %s uid %d' % (quote(path), uid), 'sif %s gid %d' % (quote(path), gid)]
    for dest, (source, executable) in sorted(files.items()):
        commands += ['rm ' + quote(dest), 'write %s %s' % (quote(source), quote(dest)),
                     'sif %s mode 0%o' % (quote(dest), 0o100000 | (0o755 if executable else 0o644))]
    for dest, to in links:
        commands += ['rm ' + quote(dest), 'symlink %s %s' % (quote(dest), quote(to))]
    r = subprocess.run(['debugfs', '-w', '-f', '-', target], input='\n'.join(commands) + '\n',
                       capture_output=True, text=True, check=True)
    bad = [l for l in (r.stdout + r.stderr).splitlines()
           if l.strip() and not l.startswith(('debugfs', 'Allocated inode')) and 'File not found by ext2_lookup' not in l]
    if bad:
        raise SystemExit('debugfs: ' + '\n'.join(bad[:10]))


def fsck_root(image):
    """Repair the image's ext4 root after a guest that was stopped without unmounting.
    e2fsck cannot reopen a filesystem at an offset after replaying its journal, so the
    partition is copied out (a reflink where the host filesystem has them) and back."""
    image = Path(image)
    offset, size = partitions(image)[1]
    part = image.with_suffix('.root.tmp')
    def copy(src, src_off, dst, dst_off):
        with open(src, 'rb') as a, open(dst, 'r+b' if Path(dst).exists() else 'w+b') as b:
            done = 0
            while done < size:
                n = os.copy_file_range(a.fileno(), b.fileno(), min(size - done, 1 << 30), src_off + done, dst_off + done)
                if n <= 0:
                    raise OSError('short copy')
                done += n
    part.unlink(missing_ok=True)
    copy(image, offset, part, 0)
    r = subprocess.run(['e2fsck', '-fy', str(part)], capture_output=True, text=True)
    if r.returncode > 3:
        part.unlink()
        raise SystemExit('e2fsck could not repair the root of %s:\n%s' % (image, (r.stdout + r.stderr)[-2000:]))
    if r.returncode:
        copy(part, 0, image, offset)
    part.unlink()
    return r.returncode


def template(args):
    up = base_image(args.kernel, args.variant)
    files = stable_payload(args.mesa, getattr(args, 'libsystem', 'spawnattr'))
    bootargs = BOOTARGS[args.kernel] + ' sfgopmode=' + args.resolution + (' -v' if args.verbose_boot else '') + (' ' + args.bootargs if args.bootargs else '')
    key = hashlib.sha256(json.dumps([str(up), bootargs, args.time_waits, TEMPLATE_VERSION,
                                     sorted((d, str(s), s.stat().st_size, s.stat().st_mtime_ns, x)
                                            for d, (s, x) in files.items())]).encode()).hexdigest()[:16]
    out = OUT / 'base' / ('template-%s-%s-%s-%s.img' % (SOURCE, args.kernel, args.variant, key))
    if out.exists():
        return out
    (OUT / 'base').mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix('.tmp')
    tmp.unlink(missing_ok=True)
    sh(['cp', '--reflink=auto', up, tmp])
    os.chmod(tmp, 0o644)
    esp = '%s@@%d' % (tmp, partitions(tmp)[0][0])
    work = OUT / 'base' / ('esp-' + key)
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    sh([tool('mcopy'), '-n', '-i', esp, '::/EFI/BOOT/kernel', work / 'kernel'])
    kernel, patched = patch_time_waits((work / 'kernel').read_bytes(), *args.time_waits)
    if not patched:
        print('qd_image: IOKitInitializeTime wait signature not found; the boot takes 40 s longer', file=sys.stderr)
    (work / 'kernel').write_bytes(kernel)
    (work / 'boot-args.txt').write_text(bootargs)
    sh([tool('mcopy'), '-o', '-i', esp, work / 'kernel', '::/EFI/BOOT/kernel'])
    sh([tool('mcopy'), '-o', '-i', esp, work / 'boot-args.txt', '::/EFI/BOOT/boot-args.txt'])
    shutil.rmtree(work)
    write_root(tmp, files)
    offset, _ = partitions(tmp)[1]
    subprocess.run(['e2fsck', '-fn', '%s?offset=%d' % (tmp, offset)], check=True, capture_output=True)
    # older templates of the same source, kernel and variant; the other source's stay. Names
    # without a source are the pin's from before the source was part of the name.
    stale = list((OUT / 'base').glob('template-%s-%s-%s-*.img' % (SOURCE, args.kernel, args.variant)))
    if SOURCE == 'pin':
        stale += list((OUT / 'base').glob('template-%s-%s-*.img' % (args.kernel, args.variant)))
    for old in stale:
        old.unlink()
    tmp.rename(out)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=Path, help='run image to write (under runs/qemu-dev)')
    p.add_argument('--fsck', type=Path, metavar='IMAGE', help='only repair the ext4 root of an existing image, then exit')
    p.add_argument('--file', action='append', default=[], metavar='SOURCE=/DEST', help='file for the run layer (executable)')
    p.add_argument('--data', action='append', default=[], metavar='SOURCE=/DEST', help='file for the run layer (mode 0644)')
    p.add_argument('--dir', action='append', default=[], metavar='/PATH:UID:GID', help='directory to make in the run layer')
    p.add_argument('--remove', action='append', default=[], metavar='/PATH', help='file to delete from the run layer')
    p.add_argument('--link', action='append', default=[], metavar='/PATH=TARGET', help='symbolic link to make in the run layer')
    p.add_argument('--overlay', action='append', default=[], type=Path, metavar='DIR',
                   help='a tree rooted at / copied onto the run layer (files and symbolic links), before --file/--data')
    p.add_argument('--variant', choices=('minimal', 'wayland'), default='minimal',
                   help='upstream image the base is made from: minimal (default) or wayland (the full no-X userland)')
    p.add_argument('--libsystem', choices=('spawnattr', 'homebrew'), default='spawnattr',
                   help='libSystem in the root: spawnattr (what the M1 RAM root has; default) or homebrew (the Homebrew stream\'s, issue 46)')
    p.add_argument('--mesa', action='store_true', help='also stage the M1 Mesa libraries (unused in QEMU)')
    p.add_argument('--verbose-boot', action='store_true', help='-v: launchd and kernel log on the serial console')
    p.add_argument('--bootargs', default='', help='extra kernel boot arguments')
    p.add_argument('--resolution', default='1024x768', help='framebuffer size: 640x480, 800x600 or 1024x768 (the firmware\'s ramfb modes; default 1024x768)')
    p.add_argument('--time-waits', type=lambda s: tuple(int(v) for v in s.split(',')), default=(1, 2),
                   help='rtc,quiet seconds for the two IOKitInitializeTime waits (default 1,2; a RELEASE kernel uses the larger for both)')
    p.add_argument('--kernel', choices=('release', 'debug'), default='release', help='upstream kernel configuration (default release)')
    p.add_argument('--refresh', action='store_true', help='ask Nix again for the inputs (after changing the audio server, say)')
    args = p.parse_args()
    global REFRESH
    REFRESH = args.refresh
    if args.fsck:
        print(json.dumps(dict(image=str(args.fsck), e2fsck_exit=fsck_root(args.fsck))))
        return
    assert args.output, '--output is required'
    out = args.output.resolve()
    assert out.is_relative_to(OUT.resolve()), 'run image must be under runs/qemu-dev'
    base = template(args)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)
    sh(['cp', '--reflink=auto', base, out])
    files, links = {}, []
    for overlay in args.overlay:
        f, l = overlay_entries(overlay)
        files.update(f)
        links += l
    links += [tuple(item.split('=', 1)) for item in args.link]
    for items, executable in ((args.file, True), (args.data, False)):
        for item in items:
            source, dest = item.split('=', 1)
            files[dest] = (Path(source).resolve(strict=True), executable)
    dirs = [(d.split(':')[0], int(d.split(':')[1]), int(d.split(':')[2])) for d in args.dir]
    if files or dirs or args.remove or links:
        write_root(out, files, dirs, args.remove, links)
    print(json.dumps(dict(image=str(out), template=str(base), files={d: str(s) for d, (s, _) in files.items()})))


if __name__ == '__main__':
    main()
