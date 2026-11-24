#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run qualified44 preparation, then replace its single ext4 input."""
from pathlib import Path
import os
import runpy
import sys

runpy.run_path(os.environ['SF_EOF_PREPARE'], run_name='__main__')
p = Path(sys.argv[2])
s = p.read_text()
old = '-kext "/nix/store/v6hx4ngcx7ydr50qk1q04pfdlwb09r3l-ext4-no-debug-map/ext4.kext"'
new = Path(os.environ['SF_EOF_EXT4']) / 'System/Library/Extensions/ext4.kext'
assert s.count(old) == 1, old
assert (new / 'Contents/MacOS/ext4').is_file()
p.write_text(s.replace(old, '-kext "' + str(new) + '"'))
print('EOF_KC retains qualified44; replaces ext4 only, no repair or durability change')
