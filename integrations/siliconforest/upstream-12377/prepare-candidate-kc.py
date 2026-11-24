#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Preserve qualified45; replace four sources and add two dormant network drivers."""
from pathlib import Path
import os,runpy,sys
runpy.run_path(os.environ['SF_SELECTED_PREPARE'],run_name='__main__')
p=Path(sys.argv[2]);s=p.read_text();root=Path(os.environ['SF_SELECTED_PROFILE'])/'System/Library/Extensions'
replacements={
    '$KEXTS/IOPCIFamily.kext':root/'IOPCIFamily.kext',
    '$KEXTS/IOUSBCompositeDriver.kext':root/'IOUSBCompositeDriver.kext',
    '/nix/store/xnamyibl5ci8asrcci8205zpa493kw0q-RavynXHCIPort-cio29-no-debug-map/RavynXHCIPort.kext':root/'RavynXHCIPort.kext',
    os.environ['SF_UPL_IMPORT_AVD']+'/System/Library/Extensions/SFAppleAVD.kext':Path(os.environ['SF_SELECTED_AVD'])/'System/Library/Extensions/SFAppleAVD.kext',
}
for old,new in replacements.items():
    old='-kext "'+old+'"';assert s.count(old)==1,old
    assert new.is_dir(),new;s=s.replace(old,'-kext "'+str(new)+'"')
anchor='  -o kernel';assert s.count(anchor)==1
names=['SFFBSDem','SFFBSDigc'] if os.environ['SF_SELECTED_NETWORK_MODULES']=='1' else []
extra=''.join('  -kext "'+str(root/(n+'.kext'))+'" \\\n' for n in names)
s=s.replace(anchor,extra+anchor);p.write_text(s)
print('Selected upstream candidate: qualified45 core/EOF retained; PCI/USB/context diagnostics; added dormant network modules:',names)
