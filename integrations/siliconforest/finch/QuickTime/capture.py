#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify fresh prefix bytes before any private framework is loaded."""
import hashlib,json,runpy,sys
from pathlib import Path
root=Path(__file__).resolve().parent
if len(sys.argv)>1 and sys.argv[1]=='capture':
 verified=[]
 for row in json.loads((root/'cold-files.json').read_text()):
  path=root/row['path']
  if not path.resolve().is_relative_to(root):raise ValueError('Stage path escape')
  actual=hashlib.file_digest(path.open('rb'),'sha256').hexdigest()
  if actual!=row['sha256']:raise ValueError('Cold stage hash mismatch: '+row['path'])
  verified.append(row)
 (root/'cold-verified.json').write_text(json.dumps(verified,indent=2)+'\n')
runpy.run_path(str(root/'compare-tests-engine.py'),run_name='__main__')
