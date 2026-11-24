#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""M1 CLI regressions and open -a checks after one cold framework transaction.

Caller holds gui.lock and bench. This opens applications but never injects input,
captures the screen, restarts the desktop, or changes preferences. dladdr proves
the installed providers are used; remaining staged dependencies are explicit.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def processes():
    listing = subprocess.run(['ps', '-A', '-o', 'pid=', '-o', 'comm='],
                             capture_output=True, text=True, check=True).stdout
    rows = []
    for line in listing.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) == 2:
            rows.append((fields[0], fields[1].rsplit('/', 1)[-1]))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--installed', action='append', required=True)
    ap.add_argument('--hardware-stage', type=Path, required=True)
    ap.add_argument('--audio-stage', type=Path, required=True)
    ap.add_argument('--extra-commands', type=Path)
    a = ap.parse_args()
    if os.uname().sysname != 'Darwin':
        raise ValueError('Target regressions require Darwin')
    remaining = a.work/'remaining-frameworks'
    remaining.mkdir()
    for path in (a.hardware_stage/'System/Library/Frameworks').iterdir():
        if path.name.removesuffix('.framework') not in a.installed:
            (remaining/path.name).symlink_to(path)
    env = {k: v for k, v in os.environ.items() if not k.startswith('DYLD_')}
    env.update(DYLD_FRAMEWORK_PATH=str(remaining), VT_AVD_TIMING='1')

    class Info(ctypes.Structure):
        _fields_ = [('file', ctypes.c_char_p), ('base', ctypes.c_void_p),
                    ('symbol', ctypes.c_char_p), ('address', ctypes.c_void_p)]
    system = ctypes.CDLL('/usr/lib/libSystem.B.dylib')
    system.dladdr.argtypes = [ctypes.c_void_p, ctypes.POINTER(Info)]
    system.dladdr.restype = ctypes.c_int
    symbols = dict(CoreVideo='CVPixelBufferGetTypeID', CoreMedia='CMTimeMake',
                   VideoToolbox='VTIsHardwareDecodeSupported',
                   AudioToolbox='AudioQueueNewOutput', AVFAudio='OBJC_CLASS_$_AVAudioEngine',
                   AVFoundation='OBJC_CLASS_$_AVURLAsset', CoreImage='kCIInputImageKey',
                   ColorSync='ColorSyncProfileCreateWithName', Accelerate='vDSP_vadd',
                   MediaAccessibility='MACaptionAppearanceGetForegroundOpacity', Metal='MTLCreateSystemDefaultDevice')
    providers = {}
    images = []
    for name in a.installed:
        if name not in symbols:
            raise ValueError('No reviewed provider probe for '+name)
        prefix = '/System/Library/Frameworks/'+name+'.framework/'
        image = ctypes.CDLL(prefix+'Versions/A/'+name)
        images.append(image)
        ptr = ctypes.cast(getattr(image, symbols[name]), ctypes.c_void_p)
        info = Info()
        if not system.dladdr(ptr, ctypes.byref(info)):
            raise RuntimeError('Provider lookup failed: '+name)
        providers[name] = info.file.decode()
        if not providers[name].startswith(prefix):
            raise RuntimeError('Installed provider was overridden: '+name)
    commands = [('vt-'+codec, [str(a.hardware_stage/'bin/vt-software-test'),
                               str(a.hardware_stage/'fixtures'/(codec+'.mp4'))], a.work)
                for codec in ['h264', 'hevc']]
    commands += [('play-'+tone, [str(a.audio_stage/'bin/sf-afplay'),
                                str(a.audio_stage/'fixtures'/tone)], a.work)
                 for tone in ['tone.wav', 'tone-aac.m4a']]
    extras = {row['name']: row for row in json.loads(a.extra_commands.read_text())} if a.extra_commands else {}
    if a.extra_commands:
        commands += [(row['name'], row['argv'], row.get('cwd', a.work))
                     for row in json.loads(a.extra_commands.read_text())]
    programs = []
    for name, argv, cwd in commands:
        digest = hashlib.sha256(Path(argv[0]).read_bytes()).hexdigest()
        start = time.monotonic()
        command_env = dict(env)
        if extras.get(name, {}).get('clean_dyld'):
            command_env = {k: v for k, v in command_env.items() if not k.startswith('DYLD_')}
        command_env.update(extras.get(name, {}).get('env', {}))
        r = subprocess.run(argv, cwd=cwd, env=command_env, capture_output=True, text=True, timeout=50)
        programs.append(dict(name=name, sha256=digest, seconds=round(time.monotonic()-start, 3),
                             status='OK' if r.returncode == 0 else 'FAIL',
                             returncode=r.returncode, stdout=r.stdout, stderr=r.stderr))
    appenv = {k: v for k, v in os.environ.items() if not k.startswith('DYLD_')}
    appenv.update(XDG_RUNTIME_DIR='/private/var/run/gpu-desktop', WAYLAND_DISPLAY='wayland-0')
    opens = []
    for app in ['Terminal', 'Activity Monitor']:
        r = subprocess.run(['open', '-a', app], env=appenv, capture_output=True, text=True, timeout=15)
        opens.append(dict(app=app, returncode=r.returncode, stdout=r.stdout, stderr=r.stderr))
    time.sleep(5)
    listing = processes()
    health = {name: [pid for pid, comm in listing if comm in [name, name+'.next']]
              for name in ['chonkstep-darwin-desktop', 'chonkstep-dock', 'coreaudiod',
                           'Terminal', 'Activity Monitor']}
    print(json.dumps(dict(providers=providers, programs=programs, app_opens=opens, processes=health)))
    return int(not all(r['returncode'] == 0 for r in programs+opens) or not all(health.values()))


if __name__ == '__main__':
    raise SystemExit(main())
