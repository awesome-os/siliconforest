#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Keep image43's cohort; pair the fixed core with the reviewed import SDK."""
from pathlib import Path
import os, runpy, sys
runpy.run_path(os.environ['SF_UPL_PREPARE'], run_name='__main__')
p = Path(sys.argv[2]); s = p.read_text()
old = '-kext "' + str(Path(os.environ['SF_FINAL_BASELINE_AVD']) / 'SFAppleAVD.kext') + '"'
avd = Path(os.environ['SF_UPL_IMPORT_AVD']) / 'System/Library/Extensions/SFAppleAVD.kext'
assert s.count(old) == 1 and (avd / 'Contents/MacOS/SFAppleAVD').is_file()
s = s.replace(old, '-kext "' + str(avd) + '"')
assert 'upl-private' in os.environ['SF_DEVFS_KERNEL']
p.write_text(s)
print('UPL_KC fixed private-page completion + reviewed import; image43 wait/HID/sound cohort retained')
