# SPDX-License-Identifier: MIT
from pathlib import Path
import sys,json,hashlib
root,manifest,phase=sys.argv[1:];m=json.loads(Path(manifest).read_text());assert hashlib.sha256((Path(root)/m['file']).read_bytes()).hexdigest()==m[phase],phase
print('Frozen ext4 EOF '+phase+' source hash verified')
