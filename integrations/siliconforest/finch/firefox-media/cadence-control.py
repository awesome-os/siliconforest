#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own public callback/admission controls; no display, pixels or audio output."""
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parent
out=root/'control-results.json'
if out.exists():raise ValueError('Control already executed; use a fresh packet')
env=os.environ.copy();env.update(SF_MEDIA_CADENCE_TRACE='1',SF_MEDIA_CADENCE_SECONDS='5',SF_FIREFOX_VIEW_STATE='1')
rows=[]
controls=[('baseline',['bin/cadence-baseline']),('observed',['bin/cadence-observed']),
    ('admission',['bin/vt-codec-admission','--admission-only','--p010','media/hevc-main10-pq.mp4'])]
if (root/'bin/view-state-control').is_file():controls.append(('view-state',['bin/view-state-control']))
for name,argv in controls:
    try:
        r=subprocess.run([str(root/argv[0])]+argv[1:],cwd=root,env=env,
                         capture_output=True,text=True,timeout=30)
        rows.append(dict(name=name,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
    except subprocess.TimeoutExpired as e:
        rows.append(dict(name=name,timeout=True,stdout=(e.stdout or b'').decode(errors='replace'),
                         stderr=(e.stderr or b'').decode(errors='replace')))
    out.write_text(json.dumps(rows,indent=2)+'\n')
    print(name,rows[-1].get('returncode','TIMEOUT'),flush=True)
if any(r.get('returncode')!=0 for r in rows[:2]):raise SystemExit(1)
# Canonical M1 P010 rejection is expected. Report it without treating it as
# an image-decode test, or requiring macOS's admission behavior on this target.
if 'VT_ADMISSION ' not in rows[2]['stdout']:raise SystemExit(1)
if len(rows)>3 and (rows[3].get('returncode')!=0 or
                   'SFVIEW_CONTROL no-AppKit-loaded=1' not in rows[3]['stdout']):raise SystemExit(1)
