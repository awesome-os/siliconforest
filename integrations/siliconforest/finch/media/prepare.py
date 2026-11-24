#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare an isolated media CLI stage and a binaries-only native reference packet.

Inputs are Linux/Nix outputs. No SSH, installation, device/output access or builds
are performed. Both destinations must be new directories.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('coremedia', 'corevideo', 'toolbox', 'avfaudio', 'decoder',
                 'media-tests', 'audio-tests', 'avaudio-tests', 'finch-source',
                 'fixture', 'stage', 'reference-packet'):
        ap.add_argument('--'+name, required=True, type=Path)
    args = ap.parse_args()
    stage, packet = args.stage, args.reference_packet
    if stage.exists() or packet.exists():
        raise ValueError('Stage and packet must not exist')
    here = Path(__file__).resolve().parent
    stage.mkdir(parents=True)
    fw = stage/'System/Library/Frameworks'
    fw.mkdir(parents=True)
    for name, root in [('CoreMedia', args.coremedia), ('CoreVideo', args.corevideo),
                       ('AudioToolbox', args.toolbox), ('AVFAudio', args.avfaudio),
                       ('VideoToolbox', args.decoder)]:
        shutil.copytree(root/'System/Library/Frameworks'/(name+'.framework'),
                        fw/(name+'.framework'), symlinks=True)
    (stage/'bin').mkdir()
    for root in (args.media_tests, args.audio_tests, args.avaudio_tests):
        for path in (root/'bin').iterdir():
            shutil.copy2(path, stage/'bin'/path.name)
    shutil.copy2(args.decoder/'usr/bin/vt-decode-test', stage/'bin')
    (stage/'fixtures').mkdir()
    shutil.copy2(args.fixture, stage/'fixtures/test-av.mp4')
    shutil.copy2(args.finch_source/'userland/tests/avaudio-buffer-test.expected', stage)
    subprocess.run([sys.executable, str(here.parent/'tools/portable-tests.py'),
                    str(here.parent.parent/'homebrew/hb_macho.py'),
                    str(stage/'bin'), str(stage/'binary-hashes.json')], check=True)
    rows = []
    for path in sorted((stage/'bin').iterdir()):
        name = path.name
        decoder = name == 'vt-decode-test'
        expected = name == 'avaudio-buffer-test'
        row = dict(name='vt-software-test' if decoder else name,
                   argv=['bin/'+name]+(['--software', 'fixtures/test-av.mp4'] if decoder else []),
                   mode='expected' if expected else 'reference',
                   skip_lines=0 if decoder or name in ('media-probe', 'audio-file-probe') else 1,
                   timeout=30, env={'DYLD_FRAMEWORK_PATH': '@ROOT@/System/Library/Frameworks'})
        if expected:
            row['expected'] = 'avaudio-buffer-test.expected'
        rows.append(row)
    (stage/'manifest.json').write_text(json.dumps(rows, indent=2)+'\n')
    shutil.copy2(here.parent/'tools/compare-tests.py', stage)
    packet.mkdir(parents=True)
    shutil.copytree(stage/'bin', packet/'bin')
    shutil.copytree(stage/'fixtures', packet/'fixtures')
    (packet/'bin').chmod(0o755)
    (packet/'fixtures').chmod(0o755)
    # Explicit native list excludes the Finch-specific malformed-archive expected test.
    refs = [{k: v for k, v in row.items() if k != 'env'} for row in rows if row['mode'] == 'reference']
    (packet/'manifest.json').write_text(json.dumps(refs, indent=2)+'\n')
    shutil.copy2(here.parent/'tools/compare-tests.py', packet)
    (packet/'SHA256SUMS').write_text(''.join(
        hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(packet))+'\n'
        for p in sorted(packet.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))


if __name__ == '__main__':
    main()
