#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute the actual USB DMA allocation method with explicit IOKit fixtures.

Argument: prepared native USB source. Native translation and mapping policy
are simulated here; these checks establish no ARM cache or device DMA fact.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]) / 'src/Kernel/Extensions/RavynXHCIPort/sf_pci_usb_dma.cpp'
s = source.read_text()
start = s.index('SFPCIUSBDMABuffer *SFPCIUSBDMABuffer::allocate(')
brace = s.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
actual = s[start:end]
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
using UInt64=uint64_t;using UInt32=uint32_t;using IOReturn=int;
static constexpr unsigned kIODirectionInOut=3,kIOMemoryPhysicallyContiguous=0x10,kIOMapWriteCombineCache=0x20;
static constexpr int kernel_task=0,kIOReturnSuccess=0;
static constexpr UInt64 page=0x4000,mask=0x0000000fffffc000ULL,limit=0x10000000;
static bool irqContext,ready=true,wrongMapper,badSegment,prepareFailure,allocationFailure;
static unsigned optionsSeen;static UInt64 spanSeen,alignmentSeen;static bool quarantine;
static bool ml_at_interrupt_context(){return irqContext;}
struct Object {virtual ~Object()=default;unsigned refs=1;void retain(){++refs;}void release(){assert(refs);--refs;}};
struct IOService:Object {
 Object *copyProperty(const char *);
 template<class T>void setProperty(const char *name,T){if(!strcmp(name,"SFCIOUSBMappingQuarantined"))quarantine=true;}
};
struct IOMapper:Object {
 bool metaCast(const char *){return !wrongMapper;}
 int callPlatformFunction(const char *,bool,IOService *,void *,void *,void *){return ready?0:1;}
 static IOMapper *copyMapperForDevice(IOService *);
};
static IOMapper mapper;
Object *IOService::copyProperty(const char *){mapper.retain();return &mapper;}
IOMapper *IOMapper::copyMapperForDevice(IOService *){mapper.retain();return &mapper;}
#define OSDynamicCast(t,p) dynamic_cast<t *>(p)
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
struct IODMACommand:Object {
 enum {kMapped=0};struct Segment64 {UInt64 fIOVMAddr=0,fLength=0;};
 static IODMACommand *withSpecification(int,int,int,int,int,UInt64,IOMapper *,void *);
 int setMemoryDescriptor(void *,bool){return prepareFailure?1:0;}
 int gen64IOVMSegments(UInt64 *,Segment64 *s,UInt32 *){s->fIOVMAddr=badSegment?0x04000001:0x04000000;s->fLength=spanSeen;return 0;}
};
static IODMACommand command;
IODMACommand *IODMACommand::withSpecification(int,int,int,int,int,UInt64,IOMapper *,void *){return &command;}
static constexpr int kIODMACommandOutputHost64=0;
struct SFPCIUSBDMABuffer:Object {
 IODMACommand *command=nullptr;IOService *endpoint=nullptr;UInt64 iova=0;
 unsigned retiring=0,unsafe=0;bool prepared=false;std::vector<unsigned char> bytes;
 bool initWithPhysicalMask(int,unsigned opts,UInt64 size,UInt64 align,UInt64){
  optionsSeen=opts;spanSeen=size;alignmentSeen=align;bytes.assign(size,0xcc);return !allocationFailure;
 }
 void *getBytesNoCopy(){return bytes.data();}
 static SFPCIUSBDMABuffer *allocate(IOService *,UInt64,UInt64);
};
static SFPCIUSBDMABuffer *last;
#define OSTypeAlloc(t) (last=new t)
ACTUAL
static void reset(){if(last){delete last;last=nullptr;}irqContext=wrongMapper=badSegment=prepareFailure=allocationFailure=quarantine=false;ready=true;}
int main(){
 IOService device;
 assert(!SFPCIUSBDMABuffer::allocate(nullptr,100,0x4000));
 irqContext=true;assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x4000));reset();
 assert(!SFPCIUSBDMABuffer::allocate(&device,0,0x4000));
 assert(!SFPCIUSBDMABuffer::allocate(&device,limit+1,0x4000));
 assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x1000));
 assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x6000));
 ready=false;assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x4000));assert(!last);reset();
 wrongMapper=true;assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x4000));assert(!last);reset();
 auto memory=SFPCIUSBDMABuffer::allocate(&device,100,0x4000);
 assert(memory && memory==last && memory->prepared && memory->endpoint==&device);
 assert(memory->iova==0x04000000 && spanSeen==0x4000 && alignmentSeen==0x4000);
 assert(optionsSeen==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapWriteCombineCache));
 for(auto byte:memory->bytes)assert(byte==0);reset();
 prepareFailure=true;assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x4000));assert(last && last->prepared && quarantine);reset();
 badSegment=true;assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x4000));assert(last && last->prepared && quarantine);reset();
 allocationFailure=true;assert(!SFPCIUSBDMABuffer::allocate(&device,100,0x4000));assert(last && !last->prepared);reset();
 // Fixture teardown above releases only simulated storage. Production pins
 // failed native command/mapper ownership until factual retirement/reboot.
}
'''.replace('ACTUAL', actual)
with tempfile.TemporaryDirectory() as temporary:
    temporary = Path(temporary)
    (temporary / 'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', str(temporary / 'test.cpp'),
                    '-o', str(temporary / 'test')], check=True)
    subprocess.run([str(temporary / 'test')], check=True)
print('actual USB DMA allocator: normal-NC owned pages, native mapper selection, zero padding, nonidentity IOVA and ambiguous preparation retention PASS (host fixtures only)')
