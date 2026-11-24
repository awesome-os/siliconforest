#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host-only actual-source eligibility, per-page branch and accounting regression.

This compiles literal frozen/patched UPL eligibility and commit branches, the
canonical-only accounting macros, the real unwire accounting prefix, and the
real active/inactive eligibility guards. VM locking, ledgers, physical pages,
pager retention and hardware DMA are outside this fixture's scope.
"""
from pathlib import Path
import hashlib,json,re,subprocess,tempfile,os
here=Path(__file__).resolve().parent
m=json.loads((here/'manifest.json').read_text());root=Path(m['source'])/'src/Kernel/xnu/osfmk/vm'
upl=(root/'vm_upl.c').read_text();resident=(root/'vm_resident.c').read_text();macros=(root/'vm_object_xnu.h').read_text()
assert hashlib.sha256(upl.encode()).hexdigest()==m['before']
def block(s,needle):
 p=s.index(needle);a=s.index('{',p);depth=0
 for i in range(a,len(s)):
  depth+=(s[i]=='{')-(s[i]=='}')
  if not depth:return s[p:i+1]
 raise ValueError(needle)
choice=block(upl,'\tif ((upl->flags & UPL_IO_WIRE) &&\n\t    !(flags & UPL_COMMIT_FREE_ABSENT) &&')
a=upl.index('\t\t\tif (fast_path_possible) {');b=upl.index('\n\t\t\tgoto commit_next_page;',a)
per_page=upl[a:b]
unwire=block(resident,'void\nvm_page_unwire(');a=unwire.index('{');b=unwire.index('#if HAS_MTE');unwire=unwire[a+1:b]+'\n}\n' # finish accounting if-block
start=macros.index('#define VM_OBJECT_WIRED_PAGE_COUNT(');end=macros.index('#define OBJECT_LOCK_SHARED',start)
account_macros=macros[start:end]
a=macros.index('boolean_t __overflow __assert_only =');b=macros.index('assert(!__overflow);',a)+len('assert(!__overflow);')
check_delta=macros[a:b].replace('\\\n','\n')
activate=block(resident[resident.index('void\nvm_page_activate('):],'\tif (m->vmp_laundry || !vm_page_is_canonical(m) ||')
deactivate=block(resident[resident.index('vm_page_deactivate_internal('):],'\tif (m->vmp_laundry || !vm_page_is_canonical(m) ||')
count_line=next(l.strip() for l in upl.splitlines() if 'VM_OBJECT_WIRED_PAGE_COUNT(shadow_object, -unwired_count)' in l)
global_line=next(l.strip() for l in upl.splitlines() if 'vm_page_wire_count -= unwired_count;' in l)
base=(here/'host.c').read_text()
with tempfile.TemporaryDirectory(prefix='sf-private-upl-') as tmp:
 tmp=Path(tmp)
 prepared=tmp/'tree';target=prepared/m['file'];target.parent.mkdir(parents=True);target.write_text(upl)
 subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(here/'kernel.patch')],cwd=prepared,check=True,capture_output=True)
 patched=target.read_text();assert hashlib.sha256(patched.encode()).hexdigest()==m['after']
 patched_choice=block(patched,'\tif ((upl->flags & UPL_IO_WIRE) &&\n\t    !(flags & UPL_COMMIT_FREE_ABSENT) &&')
 assert patched.replace(patched_choice,choice)==upl,'change outside eligibility block'
 for variant in ['frozen','patched']:
  chosen=choice if variant=='frozen' else patched_choice
  c=base
  for key,value in {'CHOICE':chosen,'PER_PAGE':per_page,'UNWIRE':unwire,'ACCOUNT_MACROS':account_macros,'CHECK_DELTA':check_delta,'ACTIVATE':activate,'DEACTIVATE':deactivate,'COUNT_LINE':count_line,'GLOBAL_LINE':global_line}.items():c=c.replace('@'+key+'@',value)
  source=tmp/(variant+'.c');source.write_text(c);binary=tmp/variant
  subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-but-set-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(source),'-o',str(binary)],check=True)
  r=subprocess.run([str(binary)]+(['private426'] if variant=='frozen' else []),capture_output=True,text=True,timeout=10)
  if variant=='frozen':
   assert r.returncode!=0 and '__overflow' in r.stderr,(r.returncode,r.stdout,r.stderr)
   print('FROZEN actual fast branch: private426 wired-count underflow assertion reproduced (expected FAIL)')
  else:
   print(r.stdout,end='');assert r.returncode==0,(r.returncode,r.stderr)
print('Private UPL host accounting/queue-guard tests PASS; no native kernel/DMA proof')
