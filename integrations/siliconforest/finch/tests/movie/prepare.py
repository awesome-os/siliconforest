#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fresh movie stage and device/window-free native reference packet. No SSH."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['program', 'fixtures', 'stage', 'reference-packet']:
        ap.add_argument('--'+name, type=Path, required=True)
    for name in ['frameworks', 'audio-stage']:
        ap.add_argument('--'+name, type=Path)
    ap.add_argument('--installed-root', action='store_true', help='Use installed providers with no DYLD overrides')
    ap.add_argument('--avfoundation', type=Path, help='Reviewed asset-reader update output')
    a = ap.parse_args()
    finch = Path(__file__).resolve().parents[2]
    if a.stage.exists() or a.reference_packet.exists():
        raise ValueError('Destinations must be new')
    if not a.installed_root:
        if not a.frameworks or not a.audio_stage:
            ap.error('Staged mode requires --frameworks and --audio-stage')
        shutil.copytree(a.frameworks/'System', a.stage/'System', symlinks=True)
    elif a.avfoundation or a.frameworks or a.audio_stage:
        ap.error('--installed-root does not accept private provider inputs')
    if not a.installed_root and a.avfoundation:
        avf = a.stage/'System/Library/Frameworks/AVFoundation.framework'
        for path, dirs, files in os.walk(avf, followlinks=False):
            Path(path).chmod(0o755)
        shutil.rmtree(avf)
        shutil.copytree(a.avfoundation/'System/Library/Frameworks/AVFoundation.framework', avf, symlinks=True)
    if not a.installed_root:
        at = a.stage/'System/Library/Frameworks/AudioToolbox.framework'
        for path, dirs, files in os.walk(at, followlinks=False):
            Path(path).chmod(0o755)
        shutil.rmtree(at)
        shutil.copytree(a.audio_stage/'System/Library/Frameworks/AudioToolbox.framework', at, symlinks=True)
    (a.stage/'bin').mkdir(parents=True)
    shutil.copy2(a.program/'bin/movie-test', a.stage/'bin')
    subprocess.run([sys.executable, str(finch/'tools/portable-tests.py'),
                    str(finch.parent/'homebrew/hb_macho.py'), str(a.stage/'bin'),
                    str(a.stage/'binary-hashes.json')], check=True)
    shutil.copytree(a.program/'Applications', a.stage/'Applications', symlinks=True)
    executable = a.stage/'Applications/FinchMovie.app/Contents/MacOS/FinchMovie'
    executable.parent.chmod(0o755)
    executable.chmod(0o755)
    shutil.copy2(a.stage/'bin/movie-test', executable)
    (a.stage/'fixtures').mkdir()
    shutil.copy2(a.fixtures/'movie.mov', a.stage/'fixtures')
    env = {'VT_AVD_TIMING': '1'}
    if not a.installed_root:
        env['DYLD_FRAMEWORK_PATH'] = '@ROOT@/System/Library/Frameworks'
    rows = [dict(name='movie-decode-only', argv=['bin/movie-test', '--decode-only', 'fixtures/movie.mov'],
                 mode='reference', skip_lines=0, timeout=30, env=env),
            dict(name='movie-headless', argv=['bin/movie-test', '--headless', 'fixtures/movie.mov'],
                 mode='exit', skip_lines=0, timeout=50, env=env)]
    (a.stage/'manifest.json').write_text(json.dumps(rows, indent=2)+'\n')
    shutil.copy2(finch/'tools/compare-tests.py', a.stage)
    packet = a.reference_packet
    packet.mkdir()
    shutil.copytree(a.stage/'bin', packet/'bin')
    shutil.copytree(a.stage/'fixtures', packet/'fixtures')
    (packet/'manifest.json').write_text(json.dumps(
        [{k: v for k, v in rows[0].items() if k != 'env'}], indent=2)+'\n')
    shutil.copy2(finch/'tools/compare-tests.py', packet)
    (packet/'SHA256SUMS').write_text(''.join(
        hashlib.sha256(path.read_bytes()).hexdigest()+'  '+str(path.relative_to(packet))+'\n'
        for path in sorted(packet.rglob('*')) if path.is_file() and path.name != 'SHA256SUMS'))


if __name__ == '__main__':
    main()
