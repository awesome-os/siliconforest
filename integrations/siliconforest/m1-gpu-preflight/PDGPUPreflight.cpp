/* Original one-shot GFX PMGR observer; never maps any GPU/ASC/UAT aperture. */
#include "PDGPUPreflight.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/OSAtomic.h>
#include <libkern/libkern.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#if !defined(__arm64__) && !defined(__aarch64__)
#error PDGPUPreflight requires ARM64
#endif
#define super IOService
OSDefineMetaClassAndStructors(PDGPUPreflight,IOService);
static volatile UInt32 gGPUPreflightClaimed=0;
extern "C" kern_return_t pd_gpu_preflight_stop(kmod_info_t *, void *) { return KERN_FAILURE; }
bool PDGPUPreflight::init(OSDictionary *dict)
{
    properties=nullptr;descriptor=nullptr;mapping=nullptr;attempted=false;
    bzero(&board,sizeof(board));bzero(&snapshot,sizeof(snapshot));
    return super::init(dict);
}
IOService *PDGPUPreflight::probe(IOService *provider,SInt32 *score)
{
    int enabled=0;
    if (!PE_parse_boot_argn("pdgpupreflight",&enabled,sizeof(enabled))||enabled!=1) return nullptr;
    if (!OSCompareAndSwap(0,1,&gGPUPreflightClaimed)) return nullptr;
    return super::probe(provider,score);
}
int PDGPUPreflight::property(void *ctx,const char *path,const char *key,
                             const uint8_t **bytes,size_t *length)
{
    PDGPUPreflight *self=static_cast<PDGPUPreflight *>(ctx);
    if (!self->properties||self->properties->getCount()>=128) return -1;
    IORegistryEntry *entry=IORegistryEntry::fromPath(path,gIODTPlane);
    if (!entry) return -1;
    OSObject *object=entry->copyProperty(key);entry->release();
    OSData *data=OSDynamicCast(OSData,object);
    OSData *copy=data&&data->getLength()<=32768?OSData::withBytes(data->getBytesNoCopy(),data->getLength()):nullptr;
    int result=-1;
    if (copy&&self->properties->setObject(copy)) {
        *bytes=static_cast<const uint8_t *>(copy->getBytesNoCopy());
        *length=copy->getLength();result=0;
    }
    if (copy) copy->release();
    if (object) object->release();
    return result;
}
int PDGPUPreflight::read32(void *ctx,uint32_t off,uint32_t *out)
{
    PDGPUPreflight *self=static_cast<PDGPUPreflight *>(ctx);
    if (!out||off!=0x3f8||self->board.pmgr_base!=UINT64_C(0x23b700000)||
        self->board.pmgr_size!=0x4000||self->snapshot.reads_attempted!=1) return -1;
    self->descriptor=IOMemoryDescriptor::withPhysicalAddress(
        self->board.pmgr_base,self->board.pmgr_size,kIODirectionIn);
    if (!self->descriptor) return -1;
    self->mapping=self->descriptor->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
    if (!self->mapping||!self->mapping->getVirtualAddress()||self->mapping->getSize()<0x4000) return -1;
    volatile const uint32_t *reg=reinterpret_cast<volatile const uint32_t *>(self->mapping->getVirtualAddress()+off);
    __asm__ volatile("dmb osh" ::: "memory");
    *out=*reg;
    __asm__ volatile("dmb osh" ::: "memory");
    return 0;
}
void PDGPUPreflight::releaseMap()
{
    if (mapping) { mapping->release();mapping=nullptr; }
    if (descriptor) { descriptor->release();descriptor=nullptr; }
}
bool PDGPUPreflight::start(IOService *provider)
{
    int enabled=0;
    if (attempted||!PE_parse_boot_argn("pdgpupreflight",&enabled,sizeof(enabled))||enabled!=1) return false;
    attempted=true;
    if (!super::start(provider)) return false;
    properties=OSArray::withCapacity(64);
    int result=properties?gpf_validate_board(property,this,&board):GPF_BOARD_REFUSED;
    if (properties) { properties->release();properties=nullptr; }
    if (result==GPF_OBSERVED) result=gpf_observe(&board,read32,this,&snapshot);
    else { snapshot.version=1;snapshot.byte_size=sizeof(snapshot);snapshot.result=result; }
    releaseMap();
    OSData *data=OSData::withBytes(&snapshot,sizeof(snapshot));
    bool providerRecorded=data&&provider&&provider->setProperty("PDGPUPreflightV1",data);
    bool instanceRecorded=data&&setProperty("PDGPUPreflightV1",data);
    IOLog("PDGPUPreflight: result=%d reads=%u completed=%u provider_preserved=%u instance_recorded=%u gpu_mmio_authorized=0 (GFX PMGR observation only)\n",
          result,snapshot.reads_attempted,snapshot.read_completed,providerRecorded?1u:0u,instanceRecorded?1u:0u);
    if (data) data->release();
    if (result!=GPF_OBSERVED||!providerRecorded||!instanceRecorded) { super::stop(provider);return false; }
    registerService();return true;
}
void PDGPUPreflight::free()
{
    releaseMap();if (properties) { properties->release();properties=nullptr; }
    super::free();
}
