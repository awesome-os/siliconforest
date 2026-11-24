#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Against qualified29: exactly kernel+SFDRMDri (devfs) and SFAgx+SFDcp (killable) changed."""
import hashlib,json,os,sys
from pathlib import Path
src=Path(os.environ.get('SF_DEVFS_VERIFY',str(Path(__file__).resolve().parent.parent/'m1-devfs-clone/verify-devfs-collection.py'))).read_text()
ns={};exec(compile(src.split('\nold,new=map(Path,sys.argv[1:])')[0],'verify-devfs-collection.py','exec'),ns)
old,new=map(Path,sys.argv[1:])
a,b=ns['modules'](old),ns['modules'](new)
assert a.keys()==b.keys()
changes=sorted(k for k in a if a[k]!=b[k])
expected=sorted(['com.apple.kernel','org.siliconforest.driver.SFDRMDri','org.siliconforest.driver.SFAgx','org.siliconforest.driver.SFDcp'])
assert changes==expected,changes
assert not any('ave' in k.lower() for k in b)
print(json.dumps({'baseline':str(old),'candidate':str(new),'changed':changes,'unchanged_modules':len(a)-4,
                  'module_count':len(b),'candidate_sha256':hashlib.sha256(new.read_bytes()).hexdigest(),
                  'installed':False,'hardware_qualified':False},indent=2))
