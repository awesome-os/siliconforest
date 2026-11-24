/* Original boot-only native private proof; no MMIO/DMA/interrupt/firmware ops. */
#if !defined(KERNEL) || !defined(__aarch64__) || !defined(__APPLE__)
#error genuine ARM64 Darwin kernel compilation required; no native success stub
#endif
#define IOKIT_ENABLE_SHARED_PTR 1
#define IOKIT_ENABLE_EXPERIMENTAL_SHARED_PTR_IN_API 1
#include "PDBCMPrivateProbe.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/OSAtomic.h>
#include <libkern/libkern.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#define super IOService
OSDefineMetaClassAndStructors(PDBCMPrivateProbe,IOService);
static_assert(sizeof(nps_expected)==216 && offsetof(nps_expected,nonce)==0 &&
              offsetof(nps_expected,sha256)==24,"exact chosen identity bytes");
static volatile UInt32 claimed;
extern "C" kern_return_t pdbcm_private_stop(kmod_info_t *,void *) {return KERN_FAILURE;}
static int labels(void *,nps_expected *out)
{
    auto chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    if(!chosen)return BCP_LABEL_ERROR;
    auto property=chosen->copyProperty("darwin-lab-platform-facts");
    auto *data=OSDynamicCast(OSData,property.get());
    if(!data || data->getLength()<NPS_HEADER_BYTES || data->getLength()>NPS_WIRE_MAX)return BCP_LABEL_ERROR;
    unsigned length=data->getLength();
    const auto *raw=static_cast<const uint8_t *>(data->getBytesNoCopy(0,length));
    if(!raw)return BCP_LABEL_ERROR;
    /* Explicit external boot quiescence/accessibility is mandatory. This
     * copies TRUSTED HOST labels; it does not independently authenticate them.
     * The actual NPP reader separately validates the complete facts/envelope. */
    if(memcmp(raw,"BCMFCT01",8))return BCP_LABEL_ERROR;
    memcpy(out,raw+64,216);
    if(data->getLength()!=length || data->getBytesNoCopy(0,length)!=raw || memcmp(out,raw+64,216))return BCP_LABEL_ERROR;
    const uint8_t *bytes=reinterpret_cast<const uint8_t *>(out);
    for(unsigned item=0;item<7;item++) {
        unsigned at=item?24+(item-1)*32:0,n=item?32:24;bool nonzero=false;
        for(unsigned i=0;i<n;i++)nonzero|=bytes[at+i]!=0;
        if(!nonzero)return BCP_LABEL_ERROR;
    }
    return 0;
}
static int configure(void *,const npp_expected *e,npp_workspace *w,npp_result *r)
{return npp_native_configure_once(e,w,r);}
static int acquire(void *,const bno_request *r,uint64_t i,uint32_t p,bno_token *t)
{return bno_native_acquire(r,i,p,t);}
static int zero(void *,const bno_token *t) {return bno_native_zero(t);}
static int copy(void *,const bno_token *t,uint64_t at,const void *p,size_t n)
{return bno_native_copy(t,at,p,n);}
static int inspect(void *,uint32_t slot,bno_snapshot *s) {return bno_native_inspect(slot,s);}
static int retire(void *,const bno_token *t) {return bno_native_retire(t);}
bool PDBCMPrivateProbe::init(OSDictionary *dict)
{
    workspace=nullptr;bcp_init(&record,1);return super::init(dict);
}
bool PDBCMPrivateProbe::publish(IOService *provider)
{
    auto bytes=OSData::withBytes(record.bytes,BCP_BYTES);
    return bytes && provider->setProperty(BCP_PROPERTY,bytes.get());
}
bool PDBCMPrivateProbe::start(IOService *provider)
{
    if(!super::start(provider) || !provider || provider!=IOService::getResourceService())return false;
    /* IOResources rematch cannot repeat even allocation/label failure. */
    if(!OSCompareAndSwap(0,1,&claimed))return false;
    retain(); /* Never release this instance/context; resident through reboot. */
    if(provider->copyProperty(BCP_PROPERTY))return false; /* never replace foreign state */
    int enabled=0;
    bool selected=PE_parse_boot_argn("pdbcmprivate",&enabled,sizeof enabled) && enabled==1;
    bcp_init(&record,selected);
    if(!selected)return publish(provider); /* zero identity, no operation attempts */
    if(!publish(provider))return false; /* coherent PENDING before work */
    workspace=static_cast<bcp_workspace *>(IOMallocZero(sizeof(bcp_workspace)));
    if(!workspace) {
        bcp_put(&record,BCP_RESULT_AT,BCP_FAILED);bcp_put(&record,BCP_ERROR_AT,static_cast<uint32_t>(BCP_MEMORY));
    } else {
        const bcp_ops ops={nullptr,labels,configure,acquire,zero,copy,inspect,retire};
        bcp_run(&ops,workspace,&record);
    }
    /* Code/context/control protection, wiring, boot quiescence and unique domain
     * ownership are PLATFORM preconditions, not established by this IOService.
     * No frees/manual release/retry on any error. Missing final publish is failure. */
    bool published=publish(provider);
    IOLog("PDBCMPrivateProbe result=%u stage=%u attempts=%u error=%d readback=0 effects=0\n",
          bcp_word(&record,BCP_RESULT_AT),bcp_word(&record,BCP_STAGE_AT),bcp_word(&record,BCP_ATTEMPTS_AT),
          static_cast<int32_t>(bcp_word(&record,BCP_ERROR_AT)));
    return published;
}
bool PDBCMPrivateProbe::terminate(IOOptionBits) {return false;}
void PDBCMPrivateProbe::stop(IOService *)
{
    /* No unregister/unload/force cleanup path is admitted. The service and all
     * controls/context stay retained; module stop also refuses. */
}
