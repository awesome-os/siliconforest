#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Two fresh-profile installed-policy gates. FX selects/rolls back under its locks.

No launcher, manifest, canonical provider, owner profile or process is changed.
Both actual GPU pictures require inspection after the native admissions pass.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    a=p.parse_args();stage=a.stage.resolve();repo=Path(__file__).resolve().parents[4]
    if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold gui.lock then bench')
    if stage.parent!=repo/'runs/sffinch' or not stage.name.startswith('firefox-media-cadence-packet-codec-'):
        raise ValueError('Own prepared codec-policy packet required')
    if (stage/'target-cold-verified.json').exists():raise ValueError('Keep the prior execution; prepare a fresh packet for any repeat')
    results=[]
    for phase in ['control','youtube-policy','av1-policy']:
        r=subprocess.run(['python3',str(Path(__file__).with_name('run-cadence.py')),str(stage),phase],cwd=repo)
        if r.returncode:return r.returncode
        if phase=='control':continue
        command=['python3',str(Path(__file__).with_name('analyze-codec-policy.py')),
            str(stage/('target-'+phase)/(phase+'-run')),'--output',str(stage/(phase+'-admission.json'))]
        if phase=='av1-policy':command.append('--switchback')
        gate=subprocess.run(command,cwd=repo)
        results.append(dict(phase=phase,returncode=gate.returncode))
        (stage/'policy-admission.json').write_text(json.dumps(dict(passed=all(r['returncode']==0 for r in results)
            and len(results)==2,phases=results,visual_review='Both actual renderer captures REQUIRED'),indent=2)+'\n')
        if gate.returncode:return gate.returncode
    return 0


if __name__=='__main__':raise SystemExit(main())
