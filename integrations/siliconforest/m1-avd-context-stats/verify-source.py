#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]);m=json.loads(Path(sys.argv[2]).read_text());phase=sys.argv[3]
for name,digest in m[phase].items():
 p=root/m['root']/name;assert hashlib.sha256(p.read_bytes()).hexdigest()==digest,(phase,name)
print('Exact frozen AVD source hashes verified',phase,len(m[phase]))
