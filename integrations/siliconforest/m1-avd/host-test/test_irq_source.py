#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the actual IRQ function plus a restored-old-placement counterexample."""
from pathlib import Path
import hashlib,os,re,subprocess,tempfile

here=Path(__file__).resolve().parent;kernel=here.parent/'kernel'
source=(kernel/'sf_avd_core.c').read_text()
def block(start,semicolon=False):
    begin=source.index(start);brace=source.index('{',begin)
    # Ignore comments and strings while retaining their offsets.
    lexical=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),source,flags=re.S)
    depth=0
    for i in range(brace,len(source)):
        if lexical[i]=='{':depth+=1
        elif lexical[i]=='}':
            depth-=1
            if depth==0:return source[begin:i+1]+(';' if semicolon else '')
    raise ValueError('Unbalanced source block')
structs=block('struct sf_avd_device {',True)+'\n'+block('struct sf_avd_session {',True)+'\n'
body=block('bool sf_avd_irq(')
anchor='\tspin_lock_irqsave(&avd->job_lock, flags);\n\tstatus = readl('
assert body.count(anchor)==1
old=body.replace(anchor,'\tstatus = readl(',1)
assert old.count('\ts = dev->current;')==1
old=old.replace('\ts = dev->current;','\tspin_lock_irqsave(&avd->job_lock, flags);\n\ts = dev->current;',1)
debug_unlock='\t\twritel_relaxed(0, avd->mbox + AVD_REG_MBOX_IRQ_ENABLE);\n\t\tspin_unlock_irqrestore(&avd->job_lock, flags);'
assert old.count(debug_unlock)==1
old=old.replace(debug_unlock,'\t\twritel_relaxed(0, avd->mbox + AVD_REG_MBOX_IRQ_ENABLE);',1)
prefix='#include "sf_linux_shim.h"\n#include "linux/avd.h"\n#include "linux/avd-regs.h"\n#include "sf_avd_core.h"\n'+structs
print('AVD_IRQ_SOURCE source-sha256='+hashlib.sha256(source.encode()).hexdigest(),flush=True)
with tempfile.TemporaryDirectory(prefix='sf-avd-irq-source-') as temporary:
    work=Path(temporary)
    for name,text,args,expected in [('old-placement',old,['--race-only'],1),('current',body,[],0)]:
        header=work/(name+'.h');header.write_text(prefix+text+'\n')
        binary=work/name
        command=[os.environ.get('CC','clang'),'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                 '-I'+str(kernel),'-I'+str(kernel/'shim'),'-DSF_IRQ_UNIT_HEADER="'+str(header)+'"',
                 str(here/'irq_source.c'),'-o',str(binary)]
        subprocess.run(command,check=True)
        run=subprocess.run([str(binary),*args],timeout=10)
        if run.returncode!=expected:raise SystemExit(name+' unexpected exit '+str(run.returncode))
        print('AVD_IRQ_SOURCE '+name+' expected-exit='+str(expected),flush=True)
