#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute actual transformed USB guard, retirement and quiesce methods.

Explicit MMIO/IOKit fixtures are host logic checks, not real halt/DMA proof.
Argument: the exact prepared native SDK source tree.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
here=Path(__file__).parents[1]
directory=Path(sys.argv[1])/'src/Kernel/Extensions/RavynXHCIPort'
s=(directory/'RavynXHCIPort.cpp').read_text()
header=(directory/'XHCI.h').read_text()
names=['XHCI_HCCPARAMS1','XHCI_HCCP1_XECP','XHCI_XECP_ID','XHCI_XECP_NEXT',
       'XHCI_XECP_ID_LEGACY_SUPPORT','XHCI_XECP_ID_SUPPORTED_PROTOCOL',
       'XHCI_LEGACY_BIOS_OWNED','XHCI_LEGACY_OS_OWNED','XHCI_LEGACY_CONTROL_STATUS',
       'XHCI_SP_MAJOR_REV','XHCI_SP_PORT_OFFSET','XHCI_SP_PORT_COUNT']
defines=[]
for name in names:
    match=re.search(r'^#define\s+'+name+r'(?:\([^\n]*?\))?\s+[^\n]+',header,re.M)
    assert match,name
    defines.append(match[0])
functions=[]
for name in ['dockFail','dockAccess','dockRead32','dockWrite32','retireDockBuffers','quiesce',
             'claimBIOSOwnership','parseExtendedCapabilities','sleepForEvent']:
    m=re.search(r'(?:bool|UInt32|void) RavynXHCIPort::'+name+r'\([^{};]*\)(?: const)?\s*\{',s);assert m,name
    end,depth=m.end(),1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    functions.append(s[m.start():end])
fixture=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "sf_usb_lifetime.h"
using UInt8=uint8_t;using UInt32=uint32_t;using UInt64=uint64_t;
static constexpr unsigned kEventWaitBackstopMs=2,kMillisecondScale=1,THREAD_UNINT=0;
static unsigned waitMilliseconds,sleeps;static uint64_t sleptUntil;
static void clock_interval_to_deadline(unsigned ms,unsigned,uint64_t *out){waitMilliseconds=ms;*out=ms;}
static void IOLockSleepDeadline(void *,void *,uint64_t until,unsigned){++sleeps;sleptUntil=until;}
static constexpr int kIOReturnSuccess=0;static bool powered,autoReset;static unsigned retired,writes;
struct Object {virtual ~Object()=default;template<class...T>void setProperty(T...){} };
struct Service:Object {int callPlatformFunction(const void *,bool,void *,void *,void *,void *){return powered?0:1;}};
struct SFPCIUSBDMABuffer:Object {int error=0;int retire(){++retired;return error;}};
struct OSArray {std::vector<Object *> entries;unsigned getCount(){return entries.size();}Object *getObject(unsigned n){return entries[n];}void flushCollection(){entries.clear();}};
#define OSDynamicCast(t,p) dynamic_cast<t *>(p)
static void IOLockLock(void *){}static void IOLockUnlock(void *){}static void IOLockWakeup(void *,void *,bool){}
#define IOLog(...) ((void)0)
#define XHCI_Log(...) ((void)0)
DEFINES
static constexpr UInt32 XHCI_USBCMD=0,XHCI_USBSTS=4,XHCI_USBCMD_RS=1,XHCI_USBCMD_INTE=4,XHCI_USBSTS_HCH=1,XHCI_USBCMD_HCRST=2,XHCI_USBSTS_CNR=0x800;
static UInt32 *registers;static void IOSleep(unsigned){if(autoReset && registers)registers[0]&=~2u;}
struct Interrupt {void disable(){}};
struct RavynXHCIPort:Object {
 Service device,host;Service *fPCI=&device,*fDockHost=&host,*fProvider=&device;
 const void *fDockAccessKey=this;OSArray array,*fDockBuffers=&array;
 void *fDockBufferLock=this,*fRemovalLock=this,*fEventWaitLock=nullptr;
 mutable UInt32 fDockMMIOFailed=0;bool fDockRetiring=false,fQuiesced=false,fDmaLive=true;
 bool fHotplugRunning=true,fBulkStop=false,fInterruptsEnabled=true;Interrupt *fInterruptSource=nullptr;
 unsigned fWorkers=0,fEventGeneration=0;int fEventWaitChannel=0,fHotplugWaitChannel=0;
 sf_usb_lifetime fLifetime{};UInt32 regs[256]{};volatile UInt8 *fCapRegs=(volatile UInt8 *)regs,*fOpRegs=fCapRegs;UInt64 fBARLength=sizeof(regs);
 UInt8 fPortMajorRev[64]{},fPairedPort[64]{};
 void dockFail()const;bool dockAccess()const;UInt32 dockRead32(volatile UInt8 *,UInt32)const;void dockWrite32(volatile UInt8 *,UInt32,UInt32)const;
 UInt32 opRead32(UInt32 n){return dockRead32(fOpRegs,n);}void opWrite32(UInt32 n,UInt32 v){++writes;dockWrite32(fOpRegs,n,v);}
 UInt32 capRead32(UInt32 n){return dockRead32(fCapRegs,n);}
 bool retireDockBuffers();void quiesce(const char *);
 void sleepForEvent(UInt32,uint64_t);
 void claimBIOSOwnership();void parseExtendedCapabilities();
 RavynXHCIPort(){powered=autoReset=true;retired=writes=0;registers=regs;regs[1]=1;}
};
FUNCTIONS
int main(){
 {RavynXHCIPort c;SFPCIUSBDMABuffer a,b;c.array.entries={&a,&b};c.quiesce("fixture");assert(c.fQuiesced && !c.fDmaLive && retired==2 && c.array.entries.empty());}
 {RavynXHCIPort c;SFPCIUSBDMABuffer a;c.array.entries={&a};powered=false;assert(c.opRead32(4)==UINT32_MAX);powered=true;c.quiesce("lost access");assert(!c.fQuiesced && c.fDmaLive && !retired && c.array.entries.size()==1);}
 {RavynXHCIPort c;SFPCIUSBDMABuffer a;c.array.entries={&a};autoReset=false;c.quiesce("reset timeout");assert(!c.fQuiesced && c.fDmaLive && !retired);}
 {RavynXHCIPort c;SFPCIUSBDMABuffer a,b;b.error=1;c.array.entries={&a,&b};c.quiesce("mapping complete failure");assert(!c.fQuiesced && c.fDmaLive && retired==2 && c.array.entries.size()==2);}
 {RavynXHCIPort c;c.fWorkers=1;c.quiesce("worker timeout");assert(!c.fQuiesced && c.fDmaLive && !retired && !writes);}
 {RavynXHCIPort c;assert(sf_usb_lifetime_enter(&c.fLifetime));c.quiesce("active operation");assert(!c.fQuiesced && !retired && !writes);}
 {RavynXHCIPort c;assert(c.dockRead32(c.fCapRegs,c.fBARLength)==UINT32_MAX && c.fDockMMIOFailed);c.dockWrite32(c.fCapRegs,0,99);assert(c.regs[0]==0 && sf_usb_lifetime_closed(&c.fLifetime) && !c.fHotplugRunning && c.fBulkStop);}
 {RavynXHCIPort c;c.regs[1]=UINT32_MAX;c.quiesce("inaccessible status");assert(!c.fQuiesced && c.fDmaLive && !retired && c.fDockMMIOFailed);}
 {RavynXHCIPort c;c.regs[XHCI_HCCPARAMS1/4]=(0x40/4)<<16;c.regs[0x40/4]=(2u<<24)|(4u<<8)|2;c.regs[0x48/4]=(2u<<8)|1;c.regs[0x50/4]=(3u<<24)|2;c.regs[0x58/4]=(2u<<8)|3;c.parseExtendedCapabilities();assert(!c.fDockMMIOFailed && c.fPortMajorRev[0]==2 && c.fPortMajorRev[2]==3 && c.fPairedPort[0]==2 && c.fPairedPort[1]==3);}
 {RavynXHCIPort c;c.regs[XHCI_HCCPARAMS1/4]=(0x40/4)<<16;c.regs[0x40/4]=1;c.regs[0x44/4]=55;c.claimBIOSOwnership();assert(!c.fDockMMIOFailed && (c.regs[0x40/4]&XHCI_LEGACY_OS_OWNED) && !c.regs[0x44/4]);}
 {RavynXHCIPort c;c.regs[XHCI_HCCPARAMS1/4]=(0x40/4)<<16;c.regs[0x40/4]=1;c.regs[0x44/4]=55;powered=false;c.claimBIOSOwnership();assert(c.fDockMMIOFailed && c.regs[0x40/4]==1 && c.regs[0x44/4]==55);}
 {RavynXHCIPort c;c.fEventWaitLock=&c;sleeps=0;c.sleepForEvent(0,5000);assert(sleeps==1 && waitMilliseconds==1000 && sleptUntil==1000);c.sleepForEvent(0,7);assert(sleptUntil==7);c.fEventGeneration=1;unsigned old=sleeps;c.sleepForEvent(0,5000);assert(sleeps==old);c.fPCI=nullptr;c.sleepForEvent(1,5000);assert(waitMilliseconds==2 && sleptUntil==2);}
 puts("actual transformed USB methods: latched physical/bounds refusal, no false all-ones halt, worker/operation/reset failures retain mappings, failed completion retains ledger PASS (host fixtures only)");
}
'''.replace('FUNCTIONS','\n'.join(functions)).replace('DEFINES','\n'.join(defines))
fixture=s[:s.index('#include')]+fixture # Retain upstream notices in the extracted-source fixture.
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp);(temp/'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-I',str(here.parent/'m1-usb-dwc3/kernel'),str(temp/'test.cpp'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
