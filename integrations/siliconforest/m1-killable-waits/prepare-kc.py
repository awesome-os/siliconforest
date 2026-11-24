#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The devfs-only frozen29 phase (m1-devfs-clone), then ONLY SFAgx and SFDcp replaced."""
from pathlib import Path
import os,runpy,sys
source,out,core,dri,agx,dcp=sys.argv[1:]
sys.argv=[os.environ.get('SF_DEVFS_PREPARE',str(Path(__file__).resolve().parent.parent/'m1-devfs-clone/prepare-devfs-kc.py')),source,out,core,dri]
runpy.run_path(sys.argv[0],run_name='__main__')
s=Path(out).read_text()
for old,new in [('/nix/store/0agqznr4ds7jvjb2x3qrzs8b4pb8dca9-PDAgx-no-debug-map/SFAgx.kext',agx),
                ('/nix/store/5k6gv6ka0r604adxlcpy2sfiphmi4m31-PDDcp-no-debug-map/SFDcp.kext',dcp)]:
    assert s.count('-kext "'+old+'"')==1,old
    assert Path(new,'Contents/MacOS',Path(new).stem).is_file(),new
    s=s.replace('-kext "'+old+'"','-kext "'+new+'"')
Path(out).write_text(s)
print('KILLABLE frozen29 phase: core/sharedDRI (devfs) + SFAgx/SFDcp (killable waits) only replaced')
