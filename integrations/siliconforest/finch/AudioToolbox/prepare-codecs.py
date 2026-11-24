#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fresh audio stage, distinct retained-PD queue provider, and file-only M5 packet."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--allow-retained-superset',action='store_true',help='Reviewed retained provider source port may add exports')
    for n in ['framework','tests','retained-pd','fixtures','stage','reference-packet']:
        ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();here=Path(__file__).resolve().parent
    if a.stage.exists() or a.reference_packet.exists():raise ValueError('Destinations must be new')
    api=runpy.run_path(str(here.parent.parent/'homebrew/hb_macho.py'))
    before=a.retained_pd.read_bytes();image=api['MachO'](before)
    required={'_AudioQueueNewOutput','_AudioQueueDispose','_AudioQueueEnqueueBuffer','_AudioConverterNew','_AudioConverterDispose'}
    if not required.issubset(image.exported()[0]):raise ValueError('Incomplete retained PD provider')
    expected=set(json.loads(line.strip()[2:]) for line in (here/'codec/retained-provider.tbd').read_text().splitlines() if line.strip().startswith('- '+chr(34)))
    actual=set(image.exported()[0])
    if expected-actual or (actual != expected and not a.allow_retained_superset):raise ValueError('Retained PD export set differs from reviewed interface')
    old=image.id['name'];new='@loader_path/AudioToolbox.output-sf'
    after,changed=api['rewrite_names'](before,lambda n:new if n==old else n);after=api['resign'](after)[0]
    if api['verify'](after) or not changed:raise ValueError('Provider install-name rewrite failed')
    fw=a.stage/'System/Library/Frameworks/AudioToolbox.framework'
    shutil.copytree(a.framework/'System/Library/Frameworks/AudioToolbox.framework',fw,symlinks=True)
    fw.chmod(0o755);(fw/'Versions/A').chmod(0o755);(fw/'Versions/A/AudioToolbox.output-sf').write_bytes(after)
    (a.stage/'retained-provider.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(before).hexdigest(),staged_sha256=hashlib.sha256(after).hexdigest(),old_install_name=old,new_install_name=new,additional_exports=sorted(actual-expected),note='Exact retained PD implementation; only install name and ad-hoc hashes change. All queue/private converter objects stay in this provider.'),indent=2)+'\n')
    shutil.copytree(a.tests/'bin',a.stage/'bin');(a.stage/'bin').chmod(0o755)
    subprocess.run([sys.executable,str(here.parent/'tools/portable-tests.py'),str(here.parent.parent/'homebrew/hb_macho.py'),str(a.stage/'bin'),str(a.stage/'binary-hashes.json')],check=True)
    (a.stage/'fixtures').mkdir()
    files=['tone.wav','tone-aac.m4a','tone-alac.m4a','tone.mp3']
    for name in files:shutil.copy2(a.fixtures/name,a.stage/'fixtures'/name)
    env={'DYLD_FRAMEWORK_PATH':'@ROOT@/System/Library/Frameworks'}
    rows=[dict(name=n,argv=['bin/'+n],mode='reference',skip_lines=1 if n=='audioconverter-test' else 0,timeout=30,env=env) for n in ['audioconverter-test','extaudiofile-test','audio-file-probe']]
    rows += [dict(name='decode-'+n,argv=['bin/audio-decode-probe','fixtures/'+n],mode='reference',skip_lines=0,timeout=30,env=env) for n in files]
    rows += [dict(name='play-'+n,argv=['bin/sf-afplay','fixtures/'+n],mode='exit',skip_lines=0,timeout=30,env=env) for n in files[:2]]
    (a.stage/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n');shutil.copy2(here.parent/'tools/compare-tests.py',a.stage)
    packet=a.reference_packet;packet.mkdir();(packet/'bin').mkdir()
    for name in ['audioconverter-test','extaudiofile-test','audio-file-probe','audio-decode-probe']:shutil.copy2(a.stage/'bin'/name,packet/'bin'/name)
    shutil.copytree(a.stage/'fixtures',packet/'fixtures')
    (packet/'manifest.json').write_text(json.dumps([{k:v for k,v in r.items() if k!='env'} for r in rows if r['mode']=='reference'],indent=2)+'\n');shutil.copy2(here.parent/'tools/compare-tests.py',packet)
    (packet/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(packet))+'\n' for p in sorted(packet.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))


if __name__=='__main__':main()
