#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply the existing exact five-module Firefox/devfs preparation, then three reviewed SDKs and the original non-import AVD."""
from pathlib import Path
import os,runpy,sys
output=Path(sys.argv[2])
runpy.run_path(os.environ['SF_COMBINED_PREPARE'],run_name='__main__')
s=output.read_text()
for old,key,name in [
 ('/nix/store/0agqznr4ds7jvjb2x3qrzs8b4pb8dca9-PDAgx-no-debug-map/SFAgx.kext','SF_FINAL_AGX','SFAgx'),
 ('/nix/store/5k6gv6ka0r604adxlcpy2sfiphmi4m31-PDDcp-no-debug-map/SFDcp.kext','SF_FINAL_DCP','SFDcp'),
 ('/nix/store/sqmbkv19ywzl11gn372jvqhld2mwmlql-PDAppleSound-no-debug-map/SFAppleSound.kext','SF_FINAL_SOUND','SFAppleSound')]:
    old='-kext "'+old+'"';assert s.count(old)==1,old
    new=Path(os.environ[key])/'System/Library/Extensions'/(name+'.kext')
    assert (new/'Contents/MacOS'/name).is_file(),new
    s=s.replace(old,'-kext "'+str(new)+'"')
old='-kext \"/nix/store/5238qcrsg1syilcp8h613qa6sdqn2d0d-puredarwin-firefox-avd-import-cache-0.1/System/Library/Extensions/SFAppleAVD.kext\"'
assert s.count(old)==1,old
avd=Path(os.environ['SF_FINAL_BASELINE_AVD'])/'SFAppleAVD.kext'
assert (avd/'Contents/MacOS/SFAppleAVD').is_file()
s=s.replace(old,'-kext \"'+str(avd)+'\"')
output.write_text(s)
print('FINAL_KC retains devfs/K14/HID, original CIO29 AVD; replaces SFAgx/SFDcp waits and SFAppleSound optional cap default0/bootarg retained')
