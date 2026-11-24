#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import hashlib,json,sys
from pathlib import Path
root,manifest,phase=sys.argv[1:];m=json.loads(Path(manifest).read_text());got=hashlib.sha256((Path(root)/m['file']).read_bytes()).hexdigest()
if got!=m[phase]:raise SystemExit('Frozen private UPL '+phase+' bytes mismatch')
print('Private UPL '+phase+' source hash verified')
