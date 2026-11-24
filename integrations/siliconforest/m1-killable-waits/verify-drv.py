#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The kext derivations here must equal the installed CIO29 ones in everything but src/name."""
import json,subprocess,sys
from pathlib import Path
here=Path(__file__).resolve().parent
installed={'build-agx.nix':'/nix/store/viln0cvz2rrrx5ks6jgwx2viaibsgm6f-puredarwin-agx-0.1.drv',
           'build-dcp.nix':'/nix/store/bfvmp659yys9rv2mvyvzdcivzj4grdbm-puredarwin-dcp-0.1.drv'}
X=['--extra-experimental-features','nix-command flakes']
def show(drv):
    d=json.loads(subprocess.run(['nix',*X,'derivation','show',drv],check=True,capture_output=True,text=True).stdout)
    d=d.get('derivations',d);return list(d.values())[0]
ok=True
for recipe,old in installed.items():
    for patched in ['false','true']:
        drv=subprocess.run(['nix-instantiate',*X,str(here/recipe),'--arg','patched',patched],check=True,capture_output=True,text=True).stdout.strip()
        a,b=show(old)['env'],show(drv)['env']
        diff=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k))
        allowed={'out','src'} if patched=='false' else {'out','src','pname','name'}
        good=set(diff)<=allowed
        if patched=='false' and b.get('src')!=a.get('src'):
            print(f'{recipe} unpatched: src {b.get("src")} vs installed {a.get("src")} (store path, not drv output: same tree bytes)')
        print(f'{recipe} patched={patched}: {drv} env diff {diff} {"OK" if good else "UNEXPECTED"}')
        ok&=good
sys.exit(0 if ok else 1)
