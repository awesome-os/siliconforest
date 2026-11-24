#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import tempfile,subprocess,json,hashlib,os
here=Path(__file__).resolve().parent;m=json.loads((here/'manifest.json').read_text());base=Path(m['source']);rel=Path(m['root'])
def block(text,needle):
 p=text.index(needle);start=text.index('{',p);depth=0
 for n in range(start,len(text)):
  depth+=(text[n]=='{')-(text[n]=='}')
  if not depth:return text[p:n+1]
 raise AssertionError(needle)
with tempfile.TemporaryDirectory(prefix='sf-avd-ctx-') as t:
 root=Path(t);tree=root/'tree'
 for name,digest in m['before'].items():
  old=(base/rel/name).read_bytes();assert hashlib.sha256(old).hexdigest()==digest;p=tree/rel/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(old)
 subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(here/'context-stats.patch')],cwd=tree,check=True,capture_output=True)
 for name,digest in m['after'].items():assert hashlib.sha256((tree/rel/name).read_bytes()).hexdigest()==digest
 text=(tree/rel/'kernel/SFAppleAVD.cpp').read_text();core=(tree/rel/'kernel/sf_avd_core.c').read_text();h=(tree/rel/'kernel/sf_avd_core.h').read_text()
 getter=block(text,'static int\ndo_context_stats(');stats=block(h,'struct sf_avd_stats {')+';'
 fixture=(here/'host.cpp').read_text().replace('@GETTER@',getter).replace('@STATS@',stats);(root/'host.cpp').write_text(fixture)
 cxx=os.environ.get('CXX','clang++');subprocess.run([cxx,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(tree/rel/'include'),str(root/'host.cpp'),'-o',str(root/'host')],check=True)
 subprocess.run([str(root/'host')],check=True,timeout=15)
 decode=block(core,'int sf_avd_session_decode(')
 assert decode.count('s->stats.jobs++;')==1 and decode.count('s->stats.done++;')==1 and decode.count('s->stats.errors++;')==2 and decode.count('s->stats.timeouts++;')==1
 # An IRQ never writes the per-session stats; session decode/read are caller-serialised.
 assert 's->stats' not in block(core,'bool sf_avd_irq(')
 fixture=(here/'decode-host.c').read_text().replace('@STATS@',stats).replace('@DECODE@',decode).replace('@SESSION_GETTER@',block(core,'void sf_avd_session_stats('))
 (root/'decode-host.c').write_text(fixture)
 subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(tree/rel/'include'),str(root/'decode-host.c'),'-o',str(root/'decode-host')],check=True)
 subprocess.run([str(root/'decode-host')],check=True,timeout=15)
 oldout=[]
 for label,include in [('before',base/rel/'include'),('after',tree/rel/'include')]:
  exe=root/label;subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(include),str(here/'abi.c'),'-o',str(exe)],check=True);oldout.append(subprocess.check_output([str(exe)]))
 assert oldout[0]==oldout[1];print('PASS all six old structure sizes/ioctl opcodes unchanged:',oldout[0].decode().strip())
 print('Source placement checks pass; owned job attribution still needs native interleaving and outcome tests')
