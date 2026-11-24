/* Source-only, one-shot register observation; kernel build/runtime unqualified. */
#include "PDDARTSurvey.h"
#include "observation_layout.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/OSAtomic.h>
#include <libkern/libkern.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#if !defined(__arm64__) && !defined(__aarch64__)
#error PDDARTSurvey requires the ARM64 J293 profile
#endif
#define super IOService
OSDefineMetaClassAndStructors(PDDARTSurvey,IOService);
static volatile UInt32 gSurveyClaimed=0;
extern "C" kern_return_t pd_dart_survey_stop(kmod_info_t *, void *) { return KERN_FAILURE; }

bool PDDARTSurvey::init(OSDictionary *dict)
{
    properties=nullptr;attempted=false;
    bzero(descriptors,sizeof(descriptors));bzero(maps,sizeof(maps));
    bzero(&board,sizeof(board));bzero(&snapshot,sizeof(snapshot));
    return super::init(dict);
}
IOService *PDDARTSurvey::probe(IOService *provider,SInt32 *score)
{
    int enabled=0;
    if (!PE_parse_boot_argn("pddartsurvey",&enabled,sizeof(enabled))||enabled!=1) return nullptr;
    // Never clear: IOResources rematching must not repeat a failed observation.
    if (!OSCompareAndSwap(0,1,&gSurveyClaimed)) return nullptr;
    return super::probe(provider,score);
}
int PDDARTSurvey::property(void *ctx,const char *path,const char *key,
                           const uint8_t **bytes,size_t *length)
{
    PDDARTSurvey *self=static_cast<PDDARTSurvey *>(ctx);
    if (!self->properties||self->properties->getCount()>=128) return -1;
    IORegistryEntry *node=IORegistryEntry::fromPath(path,gIODTPlane);
    if (!node) return -1;
    OSObject *object=node->copyProperty(key);node->release();
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
bool PDDARTSurvey::mapRegion(unsigned r)
{
    if (r>=DLS_REGIONS) return false;
    if (maps[r]) return true;
    descriptors[r]=IOMemoryDescriptor::withPhysicalAddress(board.base[r],board.size[r],kIODirectionIn);
    if (!descriptors[r]) return false;
    maps[r]=descriptors[r]->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly);
    return maps[r]&&maps[r]->getVirtualAddress()&&maps[r]->getSize()>=board.size[r];
}
int PDDARTSurvey::read32(void *ctx,unsigned r,uint32_t off,uint32_t *out)
{
    PDDARTSurvey *self=static_cast<PDDARTSurvey *>(ctx);
    if (!out||!dls_read_allowed(r,off)||r>=DLS_REGIONS||
        self->board.size[r]<4||off>self->board.size[r]-4) return -1;
    // Lazy mapping: even DART map creation follows the sampler's fresh guards.
    if (!self->mapRegion(r)) return -1;
    volatile const uint32_t *reg=reinterpret_cast<volatile const uint32_t *>(self->maps[r]->getVirtualAddress()+off);
    __asm__ volatile("dmb osh" ::: "memory");
    *out=*reg;
    __asm__ volatile("dmb osh" ::: "memory");
    return 0;
}
void PDDARTSurvey::releaseMaps()
{
    for (unsigned i=DLS_REGIONS;i>0;i--) {
        if (maps[i-1]) { maps[i-1]->release();maps[i-1]=nullptr; }
        if (descriptors[i-1]) { descriptors[i-1]->release();descriptors[i-1]=nullptr; }
    }
}
bool PDDARTSurvey::start(IOService *provider)
{
    int enabled=0;
    if (attempted||!PE_parse_boot_argn("pddartsurvey",&enabled,sizeof(enabled))||enabled!=1) return false;
    attempted=true;
    if (!super::start(provider)) return false;
    properties=OSArray::withCapacity(64);
    int result=properties?dls_validate_board(property,this,&board):DLS_IO;
    if (properties) { properties->release();properties=nullptr; }
    if (result==DLS_SAMPLED) {
        const struct dls_io io={this,read32};
        result=dls_sample(&board,&io,&snapshot);
    } else { snapshot.version=1;snapshot.result=result;snapshot.failure_region=DLS_REGIONS; }
    // Synchronous start owns all maps. No timer, interrupt or callback survives.
    releaseMaps();
    // OSData copies the bytes. IOResources retains the object independently of
    // this instance, including when a refusal below causes detach/free. Plain
    // setProperty neither publishes a resource nor requests a rematch. There
    // is no notification, retry, cleanup removal or modification of raw result.
    OSData *data=OSData::withBytes(&snapshot,sizeof(snapshot));
    bool providerRecorded=data&&provider&&provider->setProperty("PDDARTSurveyDiagnosticV1",data);
    bool instanceRecorded=data&&setProperty("PDDARTSurveyObservationV1",data);
    IOLog("PDDARTSurvey: result=%d reads=%u samples=%u last_region=%u last_offset=%u diagnostic_copy=%u provider_preserved=%u instance_recorded=%u (read-only observation, not DART initialization)\n",
          result,snapshot.reads_attempted,snapshot.sample_count,snapshot.failure_region,snapshot.failure_offset,
          data?1u:0u,providerRecorded?1u:0u,instanceRecorded?1u:0u);
    if (data) data->release();
    if (result!=DLS_SAMPLED||!providerRecorded||!instanceRecorded) { super::stop(provider);return false; }
    registerService(); // Diagnostic IOService only; no mapper/device attachment.
    return true;
}
void PDDARTSurvey::free()
{
    releaseMaps();
    if (properties) { properties->release();properties=nullptr; }
    super::free();
}
