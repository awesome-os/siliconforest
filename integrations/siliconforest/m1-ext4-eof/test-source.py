#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual-source allocation-length counterexample; no filesystem/machine writes."""
from pathlib import Path
import hashlib,json,subprocess,tempfile,os
here=Path(__file__).resolve().parent;m=json.loads((here/'manifest.json').read_text());old=(Path(m['source'])/m['file']).read_text();assert hashlib.sha256(old.encode()).hexdigest()==m['before']
core=Path(m['cluster_file']).read_text();assert hashlib.sha256(core.encode()).hexdigest()==m['cluster_sha256']
def block(text,needle):
 p=text.index(needle);a=text.index('{',p);depth=0
 for n in range(a,len(text)):
  depth+=(text[n]=='{')-(text[n]=='}')
  if not depth:return text[p:n+1]
 raise ValueError(needle)
round_line=next(l.strip() for l in core.splitlines() if 'size = (((non_rounded_size + pg_offset)' in l)
cluster=block(core,'\ncluster_io(vnode_t vp, upl_t upl');last=cluster.rindex('non_rounded_size -= io_size;');tailstop=block(cluster[last:],'if (non_rounded_size <= 0)')
fixture=(here/'host.c').read_text()
with tempfile.TemporaryDirectory(prefix='sf-ext4-eof-') as tmp:
 tmp=Path(tmp);tree=tmp/'tree';p=tree/m['file'];p.parent.mkdir(parents=True);p.write_text(old)
 subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(here/'eof.patch')],cwd=tree,check=True,capture_output=True)
 new=p.read_text();assert hashlib.sha256(new.encode()).hexdigest()==m['after']
 for variant,source in [('frozen',old),('patched',new)]:
  c=fixture
  for key,value in {'LIMIT':block(source,'static uint64_t\next4_pageout_limit('),'BLOCKMAP':block(source,'static int\next4_vnop_blockmap_impl('),'ROUND':round_line,'DECREMENT':cluster[last:last+len('non_rounded_size -= io_size;')],'TAILSTOP':tailstop}.items():c=c.replace('@'+key+'@',value)
  src=tmp/(variant+'.c');exe=tmp/variant;src.write_text(c)
  subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(src),'-o',str(exe)],check=True)
  r=subprocess.run([str(exe)]+(['eof52208'] if variant=='frozen' else []),capture_output=True,text=True,timeout=10)
  if variant=='frozen':
   assert r.returncode!=0 and 'blocks[n]' in r.stderr,(r.returncode,r.stderr);print('FROZEN actual blockmap allocates13..15 beyond EOF52208 (expected FAIL)')
  else:print(r.stdout,end='');assert r.returncode==0,(r.returncode,r.stderr)
print('Host request/allocation/cluster-tail logic PASS; no real extent/journal/locking/native proof')
