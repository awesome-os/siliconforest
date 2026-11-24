#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]);manifest=json.loads(Path(sys.argv[2]).read_text());rel=Path('src/Kernel/Extensions/SFAppleAVD')
def check(phase):
 for name,hashes in manifest.items():assert hashlib.sha256((root/rel/name).read_bytes()).hexdigest()==hashes[phase],(phase,name)
check('before');subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(Path(sys.argv[3]).resolve())],cwd=root,check=True);check('after')
