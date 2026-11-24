#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run qualified46 preparation, replace its single Wi-Fi bundle before stripping."""
from pathlib import Path
import os,runpy,sys
runpy.run_path(os.environ['SF_WIFI_PREPARE'],run_name='__main__')
p=Path(sys.argv[2]);s=p.read_text()
old='-kext "/nix/store/mfa4ny4jdfrx45alh6wh32ph034dwrwx-PDBwfm-no-debug-map/SFBwfm.kext"'
new=Path(os.environ['SF_WIFI_DRIVER'])/'System/Library/Extensions/SFBwfm.kext'
assert s.count(old)==1 and (new/'Contents/MacOS/SFBwfm').is_file()
p.write_text(s.replace(old,'-kext "'+str(new)+'"'))
print('Wi-Fi47: exact qualified46 core/other inputs retained; SFBwfm PM only')
