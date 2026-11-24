#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Keep the frozen Firefox preparation, then add only core/devfs + shared DRM."""
from pathlib import Path
import os,runpy,sys
original=os.environ['SF_FIREFOX_PREPARE']
runpy.run_path(original,run_name='__main__')
p=Path(sys.argv[2]);s=p.read_text()
core='/nix/store/9scpbhl8zk61i18sjzxkh3fwb3j83ip1-puredarwin-kernel-arm64-t8103-siliconforest-0.1'
dri='/nix/store/h7w1ph04npm64q2c25z30833qb0nmrwr-PDDRMDri-no-debug-map/SFDRMDri.kext'
for old,new in [(core,os.environ['SF_DEVFS_KERNEL']),(dri,os.environ['SF_DEVFS_DRI']+'/System/Library/Extensions/SFDRMDri.kext')]:
    assert s.count(old)==1,old
    assert Path(new).exists(),new
    s=s.replace(old,new)
p.write_text(s)
print('DEVFS_KC only core kernel + shared SFDRMDri added to the three-module Firefox delta')
