#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import hashlib,sys
source,out,core,dri=sys.argv[1:]
s=Path(source).read_text()
assert hashlib.sha256(s.encode()).hexdigest()=='f441db4633e65c3fe18016de21e8a883fec2dde9979f42c029fc8b46d452022a'
for old,new in [('/nix/store/9scpbhl8zk61i18sjzxkh3fwb3j83ip1-puredarwin-kernel-arm64-t8103-siliconforest-0.1',core),('/nix/store/h7w1ph04npm64q2c25z30833qb0nmrwr-PDDRMDri-no-debug-map/SFDRMDri.kext',dri)]:
 assert s.count(old)==1 and Path(new).exists()
 s=s.replace(old,new)
Path(out).write_text(s)
print('DEVFS_ONLY frozen29 phase: core/sharedDRI only replaced')
