#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the ACTUAL frozen and patched LinuxKPI wait/signal bodies into host-waits.cpp.

FROZEN must reproduce the counterexamples (an exiting client's syncobj wait spins, a
signalled one sleeps for ever); PATCHED must end them and change nothing else tested."""
import os,re,subprocess,tempfile,shutil
from pathlib import Path
here=Path(__file__).resolve().parent
trees={'agx':Path('/nix/store/0n94n35s2kylg2c787f94p4mm417mqhs-puredarwin-agx-tree'),
       'dcp':Path('/nix/store/gj14r67462ndvmf7f2al6ah8isjhk4wc-puredarwin-dcp-tree')}
SCHED='src/Kernel/fbsdkpi/freebsd/linuxkpi/common/src/linux_schedule.c'
KERN='src/Kernel/fbsdkpi/sf/lkpi/sf_lkpi_kern.c'
OS='src/Kernel/fbsdkpi/xnu/sf_fbsd_os.cpp'
CUR='src/Kernel/fbsdkpi/sf/lkpi/sf_linux_current.c'
def block(s,name):
    start=s.index(name)
    lex=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),s,flags=re.S)
    brace=lex.index('{',start);depth=0
    for i in range(brace,len(s)):
        depth+=(lex[i]=='{')-(lex[i]=='}')
        if not depth:return s[start:i+1]
    raise ValueError(name)
def body(root,patched):
    sched=(root/SCHED).read_text();kern=(root/KERN).read_text();os_=(root/OS).read_text();cur=(root/CUR).read_text()
    parts=[block(kern,n) for n in ['void\nsleepq_lock(','void\nsleepq_release(','void\nsleepq_add(','void\nsleepq_set_timeout(',
                                    'static int\nsleepq_sleep(','void\nsleepq_wait(','int\nsleepq_wait_sig(','int\nsleepq_timedwait(','int\nsleepq_timedwait_sig(']]
    parts.append(block(os_,'int\npdos_wait_sleep('))
    parts+= [block(sched,'static int\nlinux_add_to_sleepqueue('),block(sched,'bool\nlinux_signal_pending('),
             block(sched,'bool\nlinux_fatal_signal_pending('),block(sched,'long\nlinux_schedule_timeout(')]
    if patched:
        i=os_.index('#define SFOS_SIGKILL_MASK');parts.append(os_[i:os_.index('\n',i)])
        parts+=[block(os_,'extern "C" int\npdos_signal_pending('),block(os_,'extern "C" int\npdos_task_exiting('),block(cur,'bool\nlinux_task_exiting(')]
    return '\n'.join(parts)
with tempfile.TemporaryDirectory(prefix='sf-killable-') as tmp:
    tmp=Path(tmp)
    for tname,tree in trees.items():
        prepared=tmp/tname
        for f in [SCHED,KERN,OS,CUR]:
            (prepared/f).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(tree/f,prepared/f)
        subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(here/'killable.patch')],cwd=prepared,check=True,capture_output=True)
        for variant,root,patched in [('frozen',tree,False),('patched',prepared,True)]:
            header=tmp/f'{tname}-{variant}.h';header.write_text(body(root,patched)+'\n');binary=tmp/f'{tname}-{variant}'
            cmd=[os.environ.get('CXX','clang++'),'-std=c++14','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                 '-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                 '-DSF_WAITS_SOURCE="'+str(header)+'"']+(['-DSF_PATCHED'] if patched else [])+[str(here/'host-waits.cpp'),'-o',str(binary)]
            subprocess.run(cmd,check=True)
            r=subprocess.run([str(binary)],timeout=60,capture_output=True,text=True)
            print(f'== {tname} {variant}');print(r.stdout,end='',flush=True)
            if r.returncode:print(r.stderr);raise SystemExit(f'{tname} {variant} failed')
            if not patched:
                assert 'SPINS' in r.stdout and 'SLEEPS-FOREVER' in r.stdout,'frozen counterexample not reproduced'
print('KILLABLE_WAITS host: frozen counterexamples reproduced, patched PASS on agx+dcp trees')
