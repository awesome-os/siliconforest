#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the actual native USB admission methods with explicit host fixtures.

Exercises identity, existing Ethernet grants and BME write failures. This
neither runs IOPCIFamily nor establishes real IRQ, DMA or hot-remove safety.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

here = Path(__file__).parents[1]
source = (here / 'kernel/SFCIOPCIAdmission.cpp').read_text()
functions = []
for name in ['bindDockUSBAccess', 'admitDockUSB']:
    match = re.search(r'(?:int|IOReturn) SFCIOPCIHost::' + name + r'\([^{};]*\)\s*\{', source)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    functions.append(source[match.start():end])
fixture = r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
using UInt8=uint8_t;using UInt16=uint16_t;using UInt32=uint32_t;using IOReturn=int;
enum {kIOReturnSuccess=0,kIOReturnNotReady=1,kIOReturnUnsupported=2,kIOReturnNotPermitted=3,kIOReturnNoMemory=4,kIOReturnIOError=5};
static bool powered=true,irqReady=true,mapperReady=true,inIRQ=false;
static unsigned writes,failWrite,blocks;
static std::vector<void *> writeOrder;
struct IORegistryEntry {IORegistryEntry *parent=nullptr;unsigned refs=1;virtual ~IORegistryEntry()=default;IORegistryEntry *getParentEntry(int){return parent;}void retain(){++refs;}void release(){--refs;}template<class...T>void setProperty(T...){} };
using IOService=IORegistryEntry;
struct IOPCIDevice:IOService {
 UInt32 id=0x15d38086,code=0x060400;UInt16 command=0;UInt8 bus=0,slot=0,function=0;
 UInt32 configRead32(unsigned off){return off==0?id:code<<8;}
 UInt16 configRead16(unsigned){return command;}
 UInt8 getBusNumber(){return bus;}UInt8 getDeviceNumber(){return slot;}UInt8 getFunctionNumber(){return function;}
 void setBusMasterEnable(bool on);
};
static IOPCIDevice *mapperProvider;
struct IOMapper:IOService {
 static IOMapper *copyMapperForDevice(IOPCIDevice *){static IOMapper m;m.retain();return &m;}
 bool metaCast(const char *){return true;}IOService *getProvider(){return mapperProvider;}
 IOReturn callPlatformFunction(const char *,bool,void *,void *,void *,void *){return mapperReady?0:1;}
};
#define OSSafeReleaseNULL(p) do {if(p){(p)->release();(p)=nullptr;}}while(0)
#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
static constexpr int gIOServicePlane=0,kIORegistryIterateRecursively=0;
static std::vector<IORegistryEntry *> inventory;
struct IORegistryIterator {
 size_t n=0;static IORegistryIterator *iterateOver(IOService *,int,int){return new IORegistryIterator;}
 IORegistryEntry *getNextObject(){return n<inventory.size()?inventory[n++]:nullptr;}
 bool isValid(){return true;}void release(){delete this;}
};
static bool ml_at_interrupt_context(){return inIRQ;}static void IOLockLock(void *){}static void IOLockUnlock(void *){}
struct MSI {bool routeReady(IOPCIDevice *){return irqReady;}};
struct SFCIOPCIHost:IOService {
 struct Grant {IOPCIDevice *device;UInt32 id,code;UInt8 bus,slot,function;} grants[16]{};
 UInt32 grantCount=0,bmeActive=1;IOPCIDevice *networkEndpoint=nullptr,*usbEndpoint=nullptr;
 bool networkAttempted=true,usbAttempted=false;struct {bool admitted=true;}view;
 void *accessLock=nullptr;IOService *leaseOwner=this;MSI m,*msi=&m;
 static bool interruptPower(SFCIOPCIHost *h){return powered && h->view.admitted;}
 IOReturn blockConfiguration(){++blocks;view.admitted=false;return 0;}
 int bindDockUSBAccess(IOPCIDevice *);IOReturn admitDockUSB(IOPCIDevice *);
};
static SFCIOPCIHost *current;
void IOPCIDevice::setBusMasterEnable(bool on){
 assert(on);bool granted=false;for(unsigned n=0;n<current->grantCount;++n)if(current->grants[n].device==this)granted=true;
 assert(granted);writeOrder.push_back(this);++writes;if(writes!=failWrite)command|=4;
}
FUNCTIONS
struct Fixture {
 SFCIOPCIHost host;IOPCIDevice d[12];
 Fixture(){
 powered=irqReady=mapperReady=true;inIRQ=false;writes=failWrite=blocks=0;current=&host;inventory.clear();writeOrder.clear();
 for(unsigned n=0;n<12;++n){d[n].bus=n;d[n].parent=&host;inventory.push_back(&d[n]);}
 // Seven bridges, four actual xHCI endpoints, one I210.
 d[1].parent=&d[0];for(unsigned n=2;n<7;++n)d[n].parent=&d[1];
 for(unsigned n=7;n<11;++n){d[n].code=0x0c0330;d[n].id=n==9?0x12421b21:n==10?0x15d48086:0x11001b73;d[n].parent=&d[n-5];}
 d[11].id=0x15338086;d[11].code=0x020000;d[11].parent=&d[6];host.networkEndpoint=&d[11];mapperProvider=&d[11];
 for(unsigned n:{11u,6u,1u,0u}){auto &p=d[n];p.command=4;host.grants[host.grantCount++]={&p,p.id,p.code,p.bus,p.slot,p.function};}
 }
 void bind(){assert(!host.bindDockUSBAccess(&d[7]));assert(host.usbEndpoint==&d[7] && d[7].refs==2);}
};
int main(){
 {Fixture f;f.bind();auto prior=f.host.grants[0];assert(f.host.admitDockUSB(&f.d[7])==0);assert(writes==4 && !blocks && f.host.grantCount==6);assert((writeOrder==std::vector<void *>{&f.d[0],&f.d[1],&f.d[2],&f.d[7]}));assert(f.host.grants[0].device==prior.device && f.d[11].command==4);assert(f.d[7].command==4 && !f.d[8].command && !f.d[9].command && !f.d[10].command);assert(f.host.admitDockUSB(&f.d[7])!=0 && writes==4);}
 for(unsigned which=0;which<9;++which){Fixture f;f.bind();switch(which){case 0:powered=false;break;case 1:irqReady=false;break;case 2:mapperReady=false;break;case 3:inIRQ=true;break;case 4:f.d[8].command=4;break;case 5:f.d[11].id=0;break;case 6:f.d[2].code=0x020000;break;case 7:f.d[10].id=0xdeadbeef;break;case 8:f.d[0].bus=99;break;}assert(f.host.admitDockUSB(&f.d[7])!=0 && !writes && !blocks && f.d[11].command==4);}
 for(unsigned n=1;n<=4;++n){Fixture f;f.bind();failWrite=n; // Existing bridge BME remains set; only new writes can fail visibly.
  if(n==1 || n==2)continue;
  assert(f.host.admitDockUSB(&f.d[7])!=0 && blocks==1 && !f.host.view.admitted && f.d[11].command==4);
 }
 {Fixture f;mapperReady=false;assert(f.host.bindDockUSBAccess(&f.d[7]) && !f.host.usbEndpoint);}
 {Fixture f;assert(f.host.bindDockUSBAccess(&f.d[9]) && !f.host.usbEndpoint);}
 puts("actual native USB admission: preserves Ethernet grants, exact topology/identity/inventory, no early BME, mapper/MSI/power refusals, new-write failures quarantine PASS (host fixtures only)");
}
'''.replace('FUNCTIONS', '\n'.join(functions))
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp / 'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', str(temp / 'test.cpp'),
                    '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
