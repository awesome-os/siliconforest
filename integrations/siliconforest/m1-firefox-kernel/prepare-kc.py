#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Replace exactly three inputs in the qualified29 build phase; no boot action."""
from pathlib import Path
import hashlib
import sys

original,output,hid,spi,avd=map(Path,sys.argv[1:])
data=original.read_bytes()
assert hashlib.sha256(data).hexdigest()=='f441db4633e65c3fe18016de21e8a883fec2dde9979f42c029fc8b46d452022a'
s=data.decode()
for old,new in [('$KEXTS/IOHIDFamily.kext',hid),
                ('/nix/store/b41rq622s64yzf95nhg7zx8sp6dkc93i-PDM1SPIHID-no-debug-map/SFM1SPIHID.kext',spi),
                ('/nix/store/98npxmswr4rbvwm4x2b6898qc644mnxr-PDAppleAVD-no-debug-map/SFAppleAVD.kext',avd)]:
    old='-kext "'+old+'"';assert s.count(old)==1,old
    assert new.is_dir(),new
    s=s.replace(old,'-kext "'+str(new)+'"')
output.write_text(s)
print('Qualified29 frozen phase matched; replaced IOHIDFamily/SFM1SPIHID/SFAppleAVD only')
