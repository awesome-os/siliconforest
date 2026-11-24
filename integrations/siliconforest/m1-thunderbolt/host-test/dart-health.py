#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute actual DART health/suspend methods, including hard-IRQ fixture.

No AIC, DART MMIO, producer drain or native concurrency execution is claimed.
"""
from pathlib import Path
import os,re,subprocess,tempfile
here=Path(__file__).parents[1]
s=(here/'kernel/sf_cio_dart.cpp').read_text();functions=[]
for name in ['healthy','suspend']:
 m=re.search(r'(?:bool|int) SFCIODart::'+name+r'\(\)\s*\{',s);assert m
 end,depth=m.end(),1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 functions.append(s[m.start():end])
fixture=r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <thread>
static bool hardIRQ;static unsigned locks,writes;static bool (*healthProbe)();
static void IOLockLock(void *){assert(!hardIRQ);++locks;}static void IOLockUnlock(void *){}
static constexpr unsigned NPAGES=4;static unsigned TCR(unsigned n){return 0x100+4*n;}
struct Reg {void w32(unsigned off,unsigned value){assert(off==0x104 && !value && healthProbe && !healthProbe());++writes;}};
struct SFCIODart {
 void *lock=this;bool ready=false,poisoned=false;uint8_t busy[NPAGES]{};Reg regs[1];int invalidateError=0;
 bool healthy();int suspend();int invalidate(){if(invalidateError)__atomic_store_n(&poisoned,true,__ATOMIC_RELEASE);return invalidateError;}
};
FUNCTIONS
static SFCIODart *current;static bool probe(){return current->healthy();}
int main(){
 SFCIODart d;current=&d;healthProbe=probe;
 hardIRQ=true;assert(!d.healthy());__atomic_store_n(&d.ready,true,__ATOMIC_RELEASE);assert(d.healthy());
 __atomic_store_n(&d.poisoned,true,__ATOMIC_RELEASE);assert(!d.healthy());assert(!locks && !writes);
 __atomic_store_n(&d.poisoned,false,__ATOMIC_RELEASE);hardIRQ=false;
 d.busy[2]=1;assert(d.suspend()==EBUSY && d.healthy() && !writes);d.busy[2]=0;
 assert(!d.suspend() && !d.healthy() && writes==1);assert(!d.suspend() && writes==1);
 __atomic_store_n(&d.ready,true,__ATOMIC_RELEASE);d.invalidateError=EIO;assert(d.suspend()==EIO && !d.healthy() && d.poisoned && writes==2);
 // Concurrent cached queries while a thread publishes permanent poison.
 SFCIODart race;race.ready=true;std::thread reader([&]{for(unsigned n=0;n<100000;++n)(void)race.healthy();});
 __atomic_store_n(&race.poisoned,true,__ATOMIC_RELEASE);reader.join();assert(!race.healthy());
 puts("actual DART health/suspend: no sleeping lock in hard IRQ, atomic poison/readiness, busy refusal and revoke-before-disable on success/fault PASS (host fixtures only)");
}
'''.replace('FUNCTIONS','\n'.join(functions))
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);(tmp/'test.cpp').write_text(fixture)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-pthread','-fsanitize=address,undefined',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
 subprocess.run([str(tmp/'test')],check=True)
