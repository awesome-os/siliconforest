// SPDX-License-Identifier: MIT
// Literal PCI fallback, xHCI heap helper and descriptor walk with fake providers.
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
using UInt8=uint8_t;using UInt16=uint16_t;using UInt32=uint32_t;using UInt64=uint64_t;
struct IOPCIAddressSpace { struct { unsigned deviceNum,functionNum; } s; };
struct Bridge { unsigned supportsHotPlug,subDeviceNum; };
static unsigned reads,reply;
enum { kPCIDeviceStateNoLink=1,kPCIHotPlugTunnel=3,kPCIHPTypeMask=7,kIOPCIConfigVendorID=0 };
#define DLOG(...) ((void)0)
static uint32_t configRead32(Bridge *bridge,int reg,IOPCIAddressSpace *probe){
 assert(reg==0&&probe->s.deviceNum==bridge->subDeviceNum&&!probe->s.functionNum);reads++;return reply;
}
static unsigned fallback(unsigned noLink,unsigned hotplug,unsigned id){
 Bridge b={hotplug,5};Bridge *bridge=&b;IOPCIAddressSpace space={};reply=id;reads=0;
 @FALLBACK@
 return noLink;
}
static size_t outstanding,allocations;static bool fail_alloc;
static void *IOMalloc(size_t bytes){if(fail_alloc)return nullptr;void *p=malloc(bytes);if(p){outstanding++;allocations++;}return p;}
static void IOFree(void *p,size_t){assert(p&&outstanding);outstanding--;free(p);}
@HEAP_HELPER@
struct RavynXferSeg { UInt64 addr;UInt32 len; };
struct USBInterfaceDescriptor { UInt8 bLength,bDescriptorType,bInterfaceNumber,bAlternateSetting,bNumEndpoints,bInterfaceClass,bInterfaceSubClass,bInterfaceProtocol,iInterface; } __attribute__((packed));
struct USBEndpointDescriptor { UInt8 bLength,bDescriptorType,bEndpointAddress,bmAttributes;UInt16 wMaxPacketSize;UInt8 bInterval; } __attribute__((packed));
enum { USB_IF_CLASS_MASS_STORAGE=8,USB_IF_SUBCLASS_SCSI=6,USB_IF_PROTOCOL_BULK_ONLY=80,USB_EP_TYPE_MASK=3,USB_EP_TYPE_BULK=2,USB_EP_DIR_IN=128,USB_EP_ADDR_MASK=15 };
static bool generic(const std::vector<uint8_t>& blob){
 UInt32 totalLen=(UInt32)blob.size();uint8_t *cfgBuf=(uint8_t *)blob.data();
 @DESCRIPTOR_WALK@
 (void)bulkInMaxPkt;(void)bulkOutMaxPkt;(void)mscIface;
 return !foundMSC||!bulkInEp||!bulkOutEp||otherIface;
}
static std::vector<uint8_t> interface(unsigned cls,unsigned alt=0){return {9,4,0,(uint8_t)alt,2,(uint8_t)cls,6,80,0};}
static void append(std::vector<uint8_t>&a,const std::vector<uint8_t>&b){a.insert(a.end(),b.begin(),b.end());}
int main(){
 assert(!fallback(1,0,0x15338086)&&reads==1);assert(fallback(1,0,0xffffffff)==1&&reads==1);
 assert(fallback(1,0,0xffff0000)==1&&reads==1);assert(fallback(1,kPCIHotPlugTunnel,0x15338086)==1&&!reads);
 assert(!fallback(0,0,0x15338086)&&!reads);
 {SFXferTable<SFXferTRBSpan>a(160);SFXferTable<RavynXferSeg>b(160);assert(a.v&&b.v&&outstanding==2);a.v[159]={159,1,2};b.v[159]={0x10000,16384};}
 assert(!outstanding&&allocations==2);fail_alloc=true;{SFXferTable<SFXferTRBSpan>a(160);assert(!a.v);}assert(!outstanding);fail_alloc=false;
 auto disk=interface(8);append(disk,{7,5,129,2,0,2,0,7,5,2,2,0,2,0});assert(!generic(disk));
 auto composite=disk;append(composite,interface(3));assert(generic(composite));
 auto alternate=disk;append(alternate,interface(3,1));assert(!generic(alternate));
 assert(generic(interface(3)));assert(generic({0,4}));assert(generic({9,4,0}));assert(generic(interface(8)));
 puts("PASS actual PCI fallback (tunnels excluded), xHCI RAII/failure and mixed-interface descriptor walk");
 puts("LIMIT fake config/heap/descriptors; no bridge scan, USB transport, DMA or native enumeration proof");
}
