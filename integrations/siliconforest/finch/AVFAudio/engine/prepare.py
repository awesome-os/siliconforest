#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare fresh offline/file AVFAudio stages; never runs a program or SSH."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['engine', 'tests', 'audio-stage', 'coremedia', 'corevideo',
                 'stage', 'reference-packet']:
        ap.add_argument('--'+name, type=Path, required=True)
    a = ap.parse_args()
    here = Path(__file__).resolve().parent
    finch = here.parents[1]
    if a.stage.exists() or a.reference_packet.exists():
        raise ValueError('Destinations must be new')
    frameworks = Path('System/Library/Frameworks')
    providers = [('AVFAudio', a.engine), ('AudioToolbox', a.audio_stage),
                 ('CoreMedia', a.coremedia), ('CoreVideo', a.corevideo)]
    retained = a.audio_stage/frameworks/'AudioToolbox.framework/Versions/A/AudioToolbox.output-sf'
    if not retained.is_file():
        raise ValueError('Audio stage needs the distinct retained PD provider')
    for name, source in providers:
        fw = frameworks/(name+'.framework')
        shutil.copytree(source/fw, a.stage/fw, symlinks=True)
    shutil.copytree(a.tests/'bin', a.stage/'bin')
    (a.stage/'bin').chmod(0o755)
    subprocess.run([sys.executable, str(finch/'tools/portable-tests.py'),
                    str(finch.parent/'homebrew/hb_macho.py'), str(a.stage/'bin'),
                    str(a.stage/'binary-hashes.json')], check=True)
    shutil.copytree(a.audio_stage/'fixtures', a.stage/'fixtures')
    rows = [dict(name='avaudio-engine-test', argv=['bin/avaudio-engine-test'], skip_lines=1)]
    for name in ['tone.wav', 'tone-aac.m4a', 'tone-alac.m4a', 'tone.mp3']:
        rows.append(dict(name='avaudio-file-'+name,
                         argv=['bin/avaudio-file-codecs-probe', 'fixtures/'+name], skip_lines=0))
        if name != 'tone.wav':
            rows.append(dict(name='avaudio-converter-'+name,
                             argv=['bin/avaudio-file-codecs-probe', 'fixtures/'+name, '--raw'], skip_lines=0))
    for row in rows:
        row.update(mode='reference', timeout=30,
                   env={'DYLD_FRAMEWORK_PATH': '@ROOT@/System/Library/Frameworks'})
    (a.stage/'manifest.json').write_text(json.dumps(rows, indent=2)+'\n')
    shutil.copy2(finch/'tools/compare-tests.py', a.stage)
    receipts = []
    for name, _ in providers:
        path = a.stage/frameworks/(name+'.framework')/'Versions/A'/name
        receipts.append(dict(name=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    receipts.append(dict(name='AudioToolbox.output-sf', sha256=hashlib.sha256(retained.read_bytes()).hexdigest()))
    (a.stage/'provider-hashes.json').write_text(json.dumps(receipts, indent=2)+'\n')
    packet = a.reference_packet
    packet.mkdir()
    shutil.copytree(a.stage/'bin', packet/'bin')
    shutil.copytree(a.stage/'fixtures', packet/'fixtures')
    (packet/'manifest.json').write_text(json.dumps(
        [{k: v for k, v in row.items() if k != 'env'} for row in rows], indent=2)+'\n')
    shutil.copy2(finch/'tools/compare-tests.py', packet)
    (packet/'SHA256SUMS').write_text(''.join(
        hashlib.sha256(path.read_bytes()).hexdigest()+'  '+str(path.relative_to(packet))+'\n'
        for path in sorted(packet.rglob('*')) if path.is_file() and path.name != 'SHA256SUMS'))


if __name__ == '__main__':
    main()
