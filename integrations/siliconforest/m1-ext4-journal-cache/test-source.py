#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply to exact frozen bytes; compile actual reader/overlay against failing I/O."""
from pathlib import Path
import json,hashlib,tempfile,subprocess,os
here=Path(__file__).resolve().parent
frozen=Path('/nix/store/ssrsapdr7x9c2s7q3kspfk4qlkv89n9d-puredarwin-ext4-downgrade-tree')
m=json.loads((here/'manifest.json').read_text())
def block(s,needle):
 start=s.index(needle);p=s.index('{',start);depth=0
 for end in range(p,len(s)):
  depth+=(s[end]=='{')-(s[end]=='}')
  if not depth:return s[start:end+1]
 raise AssertionError(needle)
with tempfile.TemporaryDirectory(prefix='sf-ext4-cache-') as tmp:
 root=Path(tmp);p=root/m['file'];p.parent.mkdir(parents=True);p.write_bytes((frozen/m['file']).read_bytes());assert hashlib.sha256(p.read_bytes()).hexdigest()==m['before_sha256']
 lp=root/m['lifecycle_file'];lp.write_bytes((frozen/m['lifecycle_file']).read_bytes());assert hashlib.sha256(lp.read_bytes()).hexdigest()==m['lifecycle_before_sha256']
 subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(here/'journal-cache.patch')],cwd=root,check=True);s=p.read_text();assert hashlib.sha256(p.read_bytes()).hexdigest()==m['after_sha256'];assert hashlib.sha256(lp.read_bytes()).hexdigest()==m['lifecycle_after_sha256']
 template=(here/'host-cache.c').read_text();values={'COUNT':block(s,'static void\npd_ext4_cache_count('),'OVERLAY':block((frozen/'src/Kernel/Extensions/ext4/ext4_jbd.c').read_text(),'int\next4_jnl_read_overlay('),'READER':block(s,'int\next4_blkread(')}
 for key,value in values.items():template=template.replace('@'+key+'@',value)
 test=root/'host.c';test.write_text(template)
 subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(test),'-o',str(root/'host')],check=True)
 subprocess.run([str(root/'host')],check=True,timeout=15)
