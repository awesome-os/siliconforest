// SPDX-License-Identifier: GPL-2.0
// Fixed PCIe-C domain, separate from the NHI's SID1/0x200-root DART.
#include "sf_cio_pci_mapper.h"
#include "sf_usb_iova.h"
#include "sf_pciec_dma_inspect.h"
#include "sf_cpu_par.h"
#include <IOKit/IORegistryEntry.h>
#include <machine/machine_routines.h>
#include <mach/vm_param.h>
OSDefineMetaClassAndStructors(SFCIOPCIDartMapper,IOMapper)
static constexpr uint64_t pciPage=0x4000,pciMask=0x0000000fffffc000ULL;
static constexpr uint64_t pciLo=0x04000000,pciBytes=0x10000000;
static constexpr unsigned pciPages=pciBytes/pciPage,pciMappings=2048;
bool SFCIOPCIDartMapper::initWithController(SFCIOPCIController *ctl,IOPCIDevice *dev)
{
 controller=NULL;endpoint=NULL;lock=NULL;busy=NULL;mappings=NULL;
 bzero(usbEndpoints,sizeof(usbEndpoints));usbEndpointCount=0;
 tableCount=mappingCount=0;setupAttempted=ready=touched=poisoned=revoked=false;
 bzero(tables,sizeof(tables));pt={};io={};fIsSystem=false;
 if(!ctl || !dev || !IOMapper::init())return false;
 lock=IOLockAlloc();if(!lock)return false;
 ctl->retain();controller=ctl;dev->retain();endpoint=dev;
 pt.ops={this,table,allocateTable};io={{this,read,write,delay},publish};return true;
}
bool SFCIOPCIDartMapper::inventoryBlocked()
{
 if(!endpoint || endpoint->configRead32(0)!=0x15338086 ||
    (endpoint->configRead32(8)>>8)!=0x020000 ||
    endpoint->getDeviceNumber() || endpoint->getFunctionNumber())return false;
 IORegistryEntry *host=endpoint;unsigned depth=0;
 while(host && !host->metaCast("SFCIOPCIHost") && depth++<64)host=host->getParentEntry(gIOServicePlane);
 if(!host || !host->metaCast("SFCIOPCIHost"))return false;
 auto it=IORegistryIterator::iterateOver(host,gIOServicePlane,kIORegistryIterateRecursively);
 if(!it)return false;
 unsigned seen=0,xhci=0,i210=0;bool found=false,ok=true;
 while(auto obj=it->getNextObject()){
  if(++seen>256){ok=false;break;}
  auto dev=OSDynamicCast(IOPCIDevice,obj);if(!dev)continue;
  uint32_t id=dev->configRead32(0),cls=dev->configRead32(8)>>8;
  if(id==UINT32_MAX || (dev->configRead16(4)&4)){ok=false;break;}
  if(cls==0x0c0330)++xhci;
  else if(id==0x15338086 && cls==0x020000)++i210;
  else if((cls>>8)!=0x0604){ok=false;break;}
  if(dev==endpoint)found=true;
 }
 if(!it->isValid())ok=false;
 it->release();return ok && found && xhci==4 && i210==1;
}
bool SFCIOPCIDartMapper::live()
{
 return !revoked && !poisoned && controller && controller->dmaOwner==this &&
  controller->endpointAccess();
}
void SFCIOPCIDartMapper::poison()
{
 poisoned=true;ready=false;
 // Blocks all later accesses through this controller backend.
 // Current enumeration host always strips BME, independent of this object.
 controller->poisoned=true;
}
int SFCIOPCIDartMapper::read(void *p,uint32_t off,uint32_t *value)
{
 auto s=(SFCIOPCIDartMapper *)p;
 bool word=!(off&3) && ((off>=0x100 && off<0x200) || (off>=0x400 && off<0x800));
 if(!value || !(word || off==0x20 || off==0x40 || off==0x50 || off==0x54 || off==0x60 || off==0xfc))return EINVAL;
 if(!s->live())return ENODEV;
 __asm__ volatile("dsb sy; isb" ::: "memory");
 *value=s->controller->endpointDart.r32(off);
 __asm__ volatile("dsb sy; isb" ::: "memory");return 0;
}
int SFCIOPCIDartMapper::write(void *p,uint32_t off,uint32_t value)
{
 auto s=(SFCIOPCIDartMapper *)p;
 if(!(off==0x100 || (off>=0x400 && off<0x410 && !(off&3)) ||
      off==0x20 || off==0x34 || off==0x38 || off==0xfc))return EINVAL;
 if(!s->live())return ENODEV;
 __asm__ volatile("dsb sy; isb" ::: "memory");s->controller->endpointDart.w32(off,value);
 __asm__ volatile("dsb sy; isb" ::: "memory");return 0;
}
void SFCIOPCIDartMapper::delay(void *,unsigned us){IODelay(us);}
int SFCIOPCIDartMapper::publish(void *p)
{
 auto s=(SFCIOPCIDartMapper *)p;
 if(!s->live() || !s->pt.l1 || !s->tableCount)return ENODEV;
 __asm__ volatile("dsb sy" ::: "memory");return 0;
}
uint64_t *SFCIOPCIDartMapper::table(void *p,uint64_t physical)
{
 auto s=(SFCIOPCIDartMapper *)p;
 for(unsigned n=0;n<s->tableCount;++n)if(s->tables[n].physical==physical)
  return (uint64_t *)s->tables[n].memory->getBytesNoCopy();
 return NULL;
}
uint64_t SFCIOPCIDartMapper::allocateTable(void *p)
{
 auto s=(SFCIOPCIDartMapper *)p;
 if(s->tableCount==16)return 0;
 auto md=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
  kIODirectionInOut|kIOMemoryPhysicallyContiguous,pciPage,pciMask);
 if(!md)return 0;
 IOByteCount span=0;uint64_t pa=md->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 if(!pa || (pa&~pciMask) || span<pciPage){md->release();return 0;}
 bzero(md->getBytesNoCopy(),pciPage);s->tables[s->tableCount++]={md,pa};return pa;
}
bool SFCIOPCIDartMapper::initHardware(IOService *provider)
{
 if(provider!=endpoint || PAGE_SIZE!=pciPage || ml_at_interrupt_context() || !lock)return false;
 IOLockLock(lock);bool attempted=setupAttempted;setupAttempted=true;IOLockUnlock(lock);
 if(attempted || !inventoryBlocked())return false;
 // No controller lock around reserveRequester: it takes that lock itself.
 uint32_t sid=UINT32_MAX;
 int error=controller->reserveRequester(endpoint->getBusNumber(),endpoint->getDeviceNumber(),endpoint->getFunctionNumber(),&sid);
 if(error || sid)return false;
 IOLockLock(lock);IOLockLock(controller->lock);
 if(ready || touched || controller->dmaOwner || !controller->endpointAccess()){
  IOLockUnlock(controller->lock);IOLockUnlock(lock);return false;
 }
 busy=(uint8_t *)IOMallocZero(pciPages);
 mappings=(Mapping *)IOMallocZero(sizeof(Mapping)*pciMappings);
 uint64_t root=allocateTable(this);pt.l1=table(this,root);
 if(!busy || !mappings || !root){IOLockUnlock(controller->lock);IOLockUnlock(lock);return false;}
 controller->dmaOwner=this;
 // A real self-reference keeps the owner/endpoint/tables alive even when
 // start fails after an ambiguous hardware write and its caller releases
 // the service. It is never dropped without a future real teardown path.
 retain();
 error=sf_pciec_dart_sid0_prepare(&io,root,&touched);
 bool unpin=error && !touched;
 if(error && touched)poison();
 else if(error)controller->dmaOwner=NULL;
 else ready=true;
 IOLockUnlock(controller->lock);IOLockUnlock(lock);
 setProperty("endpoint-DMA-admitted",false);setProperty("sid",(uint32_t)0,32);
 setProperty("domain-setup-error",(uint32_t)error,32);
 if(unpin)release();return !error;
}
uint64_t SFCIOPCIDartMapper::getPageSize() const{return pciPage;}
bool SFCIOPCIDartMapper::knownEndpoint(IOService *device) const
{
 if(device==endpoint)return true;
 for(unsigned n=0;n<usbEndpointCount;++n)if(device==usbEndpoints[n])return true;
 return false;
}
IOReturn SFCIOPCIDartMapper::registerDockUSB(IOPCIDevice *device)
{
 if(!device || !endpoint || ml_at_interrupt_context() || !lock ||
    device->getDeviceNumber() || device->getFunctionNumber() ||
    (device->configRead32(8)>>8)!=0x0c0330 || (device->configRead16(4)&4))return kIOReturnBadArgument;
 UInt32 id=device->configRead32(0);
 if(id!=0x11001b73 && id!=0x12421b21 && id!=0x15d48086)return kIOReturnUnsupported;
 auto hostFor=[](IORegistryEntry *node)->IORegistryEntry *{
  unsigned depth=0;
  while(node && !node->metaCast("SFCIOPCIHost") && depth++<64)node=node->getParentEntry(gIOServicePlane);
  return node && node->metaCast("SFCIOPCIHost")?node:NULL;
 };
 auto host=hostFor(endpoint);
 if(!host || hostFor(device)!=host || device->getProperty("iommu-parent"))return kIOReturnNotPermitted;
 // Query fresh TTBR/TCR/gate ownership before adding another requester to
 // this root. Never call reserveRequester while holding controller->lock.
 auto query=OSSymbol::withCString("SFCIOPCIDMAReady");
 if(!query)return kIOReturnNoMemory;
 IOReturn owned=callPlatformFunction(query,false,endpoint,NULL,NULL,NULL);
 query->release();if(owned!=kIOReturnSuccess)return kIOReturnNotReady;
 IOLockLock(lock);bool prior=knownEndpoint(device),full=usbEndpointCount==4;IOLockUnlock(lock);
 if(prior || full)return kIOReturnNoResources;
 uint32_t sid=UINT32_MAX;
 if(controller->reserveRequester(device->getBusNumber(),device->getDeviceNumber(),device->getFunctionNumber(),&sid) || sid)
  return kIOReturnIOError;
 IOLockLock(lock);IOLockLock(controller->lock);
 bool ok=ready && live() && usbEndpointCount<4 && !knownEndpoint(device);
 if(ok && sf_pciec_dart_sid0_check(&io)){poison();ok=false;}
 if(ok){device->retain();usbEndpoints[usbEndpointCount++]=device;}
 IOLockUnlock(controller->lock);IOLockUnlock(lock);
 // The factory must bind this actual mapper object and verify native lookup
 // before matching a driver. Registration alone publishes no such binding.
 return ok?kIOReturnSuccess:kIOReturnNotReady;
}
IOReturn SFCIOPCIDartMapper::callPlatformFunction(const OSSymbol *name,bool wait,
 void *p1,void *p2,void *p3,void *p4)
{
 if(name && name->isEqualTo(SF_PCIEC_DMA_INSPECT_FUNCTION)){
  if(!p1 || !p2 || p3 || p4 || wait)return kIOReturnBadArgument;
  return inspectDMA((IOService *)p1,p2);
 }
 if(!name || !name->isEqualTo("SFCIOPCIDMAReady"))
  return IOMapper::callPlatformFunction(name,wait,p1,p2,p3,p4);
 if(!p1 || p2 || p3 || p4 || ml_at_interrupt_context() || !lock || !controller)
  return kIOReturnBadArgument;
 IOLockLock(lock);IOLockLock(controller->lock);
 bool ok=knownEndpoint((IOService *)p1) && ready && live();
 if(ok){
  uint32_t tcr=0,root=0,gate=0;
  int error=sf_pciec_dart_sid0_check(&io);
  if(!error)error=read(this,0x100,&tcr);
  if(!error)error=read(this,0x400,&root);
  if(!error)error=read(this,0xfc,&gate);
  ok=!error && tableCount && tcr==(1u<<7) && (gate&1) &&
   root==((1u<<31)|(uint32_t)(tables[0].physical>>12));
  if(!ok)poison();
 }
 IOLockUnlock(controller->lock);IOLockUnlock(lock);
 // Translation ownership/readback only. Not an endpoint DMA/drain ACK.
 return ok?kIOReturnSuccess:kIOReturnNotReady;
}
IOReturn SFCIOPCIDartMapper::inspectDMA(IOService *device,void *output)
{
 auto snapshot=(sf_pciec_dma_inspect *)output;
 if(!device || !snapshot || ml_at_interrupt_context() || !lock || !controller ||
    snapshot->version!=SF_PCIEC_DMA_INSPECT_VERSION || snapshot->size!=sizeof(*snapshot))
  return kIOReturnBadArgument;
 IOLockLock(lock);IOLockLock(controller->lock);
 // Identity and current ownership first. Do not use the normal health check:
 // it poisons on a valid fault before the fault itself could be captured.
 bool ok=device==endpoint && ready && live() && tableCount && mappings;
 if(ok){
  bzero(snapshot,sizeof(*snapshot));
  snapshot->version=SF_PCIEC_DMA_INSPECT_VERSION;snapshot->size=sizeof(*snapshot);
  snapshot->mapping_count=mappingCount;snapshot->table_count=tableCount;
  static const uint32_t offsets[SF_PCIEC_DMA_INSPECT_REGS]={
   0x20,0x40,0x50,0x54,0x60,0xfc,0x100,0x400,0x404,0x408,0x40c,0x104};
  for(unsigned n=0;n<SF_PCIEC_DMA_INSPECT_REGS;++n)
   if(read(this,offsets[n],&snapshot->regs[n])){ok=false;break;}
  for(unsigned n=0;ok && n<64;++n){
   if(SFCIOPCIController::read(controller,SF_PCIEC_PORT,0x828+4*n,&snapshot->rid2sid[n]) ||
      read(this,0x100+4*n,&snapshot->tcr[n]))ok=false;
  }
  if(ok){
   snapshot->root_physical=tables[0].physical;
   snapshot->root_cpu_par=sf_cpu_par(tables[0].memory->getBytesNoCopy());
   // First two live allocations are normally the rings; last two include
   // current packet bounces. Select retained objects, never an input IOVA.
   unsigned first[2]={pciMappings,pciMappings},last[2]={pciMappings,pciMappings};
   for(unsigned n=0;n<pciMappings;++n)if(mappings[n].memory){
    if(first[0]==pciMappings)first[0]=n;
    else if(first[1]==pciMappings)first[1]=n;
    last[0]=last[1];last[1]=n;
   }
   unsigned slots[4]={first[0],first[1],last[0],last[1]};
   for(unsigned n=0;n<SF_PCIEC_DMA_INSPECT_SAMPLES;++n){
    auto &s=snapshot->sample[n];s.slot=slots[n];
    if(s.slot==pciMappings)continue;
    auto &m=mappings[s.slot];s.iova=m.address;s.bytes=m.bytes;
    IOByteCount span=0;s.physical=m.memory->getPhysicalSegment(0,&span,kIOMemoryMapperNone);s.span=span;
    sf_dart_walk walked={};s.walk_error=sf_dart_table_walk(&pt,m.address,&walked);
    s.walked=walked.phys;s.l1e=walked.l1e;s.pte=walked.pte;
    auto bytes=(volatile uint64_t *)m.memory->getBytesNoCopy();
    s.cpu_par=sf_cpu_par((const void *)bytes);
    if(m.bytes>=sizeof(s.words))for(unsigned q=0;q<4;++q)s.words[q]=bytes[q];
   }
  }
 }
 IOLockUnlock(controller->lock);IOLockUnlock(lock);
 // Read-only CPU view of retained tables/backing plus current DART words.
 // Neither a device translation walk nor a DMA completion acknowledgement.
 return ok?kIOReturnSuccess:kIOReturnNotReady;
}
IOReturn SFCIOPCIDartMapper::iovmMapMemory(IOMemoryDescriptor *memory,uint64_t offset,
 uint64_t length,uint32_t options,const IODMAMapSpecification *spec,IODMACommand *command,
 const IODMAMapPageList *,uint64_t *address,uint64_t *mappedLength)
{
 auto md=OSDynamicCast(IOBufferMemoryDescriptor,memory);
 uint32_t allowed=kIODMAMapReadAccess|kIODMAMapWriteAccess|kIODMAMapPhysicallyContiguous|kIODMAMapPageListFullyOccupied;
 if(!md || !command || !address || !mappedLength || offset || !length || length>pciBytes ||
    length!=md->getLength() || (length&(pciPage-1)) || (options&~allowed) ||
    !(options&(kIODMAMapReadAccess|kIODMAMapWriteAccess)) || ml_at_interrupt_context())return kIOReturnBadArgument;
 uint64_t alignment=spec && spec->alignment?spec->alignment:pciPage;
 if(alignment&(alignment-1))return kIOReturnUnsupported;
 if(alignment<pciPage)alignment=pciPage;
 if(alignment>pciBytes || (spec && spec->options))return kIOReturnUnsupported;
 unsigned bits=spec && spec->numAddressBits?spec->numAddressBits:32;
 if(bits>64)return kIOReturnBadArgument;
 if(bits<27)return kIOReturnNoResources;
 IOByteCount span=0;uint64_t pa=md->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 if(!pa || (pa&~pciMask) || span<length || length-1>((1ULL<<36)-1)-pa)return kIOReturnBadArgument;
 IOLockLock(lock);IOLockLock(controller->lock);
 if(spec && spec->device && !knownEndpoint(spec->device)){
  IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnNotPermitted;
 }
 if(!ready || !live()){IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnNotReady;}
 if(sf_pciec_dart_sid0_check(&io)){
  poison();IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnIOError;
 }
 for(unsigned n=0;n<pciMappings;++n)if(mappings[n].memory==md){
  IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnBusy;
 }
 unsigned slot=0;while(slot<pciMappings && mappings[slot].memory)++slot;
 size_t pages=length/pciPage,first=sf_usb_iova_find(busy,pciPages,pciLo,pciPage,pages,alignment);
 uint64_t dva=first==SIZE_MAX?0:pciLo+first*pciPage;
 if(slot==pciMappings || !dva || (bits<32 && dva+length>(1ULL<<bits))){
  IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnNoResources;
 }
 // Reserve and retain before PTE writes, including ambiguous flush failure.
 md->retain();mappings[slot]={md,command,dva,length};++mappingCount;memset(busy+first,1,pages);
 unsigned protection=(options&kIODMAMapReadAccess?SF_DART_PROT_READ:0)|
  (options&kIODMAMapWriteAccess?SF_DART_PROT_WRITE:0);
 int error=sf_dart_table_map(&pt,dva,pa,length,protection);
 // Even a rolled-back map may have published a new table. Flush, but keep
 // every reservation/backing on any PTE error or ambiguous completion.
 int flushed=sf_pciec_dart_sid0_check(&io);
 if(!flushed)flushed=sf_pciec_dart_sid0_flush(&io);
 if(flushed || error)poison();
 else{*address=dva;*mappedLength=length;}
 IOLockUnlock(controller->lock);IOLockUnlock(lock);
 return error || flushed?kIOReturnIOError:kIOReturnSuccess;
}
IOReturn SFCIOPCIDartMapper::iovmUnmapMemory(IOMemoryDescriptor *memory,IODMACommand *command,
 uint64_t address,uint64_t length)
{
 if(!memory || !command || !length || ml_at_interrupt_context())return kIOReturnBadArgument;
 IOLockLock(lock);IOLockLock(controller->lock);
 if(!ready || !live()){IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnNotReady;}
 unsigned slot=0;
 while(slot<pciMappings && !(mappings[slot].memory==memory && mappings[slot].command==command && mappings[slot].address==address && mappings[slot].bytes==length))++slot;
 if(slot==pciMappings){IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnBadArgument;}
 if(sf_pciec_dart_sid0_check(&io)){
  poison();IOLockUnlock(controller->lock);IOLockUnlock(lock);return kIOReturnIOError;
 }
 uint64_t removed=sf_dart_table_unmap(&pt,address,length);
 int error=sf_pciec_dart_sid0_check(&io);
 if(!error)error=sf_pciec_dart_sid0_flush(&io);
 if(removed!=length || error)poison();
 auto md=mappings[slot].memory;bool release=!poisoned;
 if(release){mappings[slot]={};--mappingCount;memset(busy+(address-pciLo)/pciPage,0,length/pciPage);}
 IOLockUnlock(controller->lock);IOLockUnlock(lock);
 if(release)md->release();return release?kIOReturnSuccess:kIOReturnIOError;
}
IOReturn SFCIOPCIDartMapper::iovmInsert(uint32_t,uint64_t,uint64_t,uint64_t,uint64_t)
{return kIOReturnUnsupported;}
uint64_t SFCIOPCIDartMapper::mapToPhysicalAddress(uint64_t address)
{
 if(ml_at_interrupt_context() || !lock || !controller)return 0;
 IOLockLock(lock);IOLockLock(controller->lock);uint64_t physical=0;
 if(ready && live())for(unsigned n=0;n<pciMappings;++n){
  auto &m=mappings[n];if(!m.memory || address<m.address || address-m.address>=m.bytes)continue;
  sf_dart_walk walk={};if(!sf_dart_table_walk(&pt,address,&walk))physical=walk.phys;break;
 }
 IOLockUnlock(controller->lock);IOLockUnlock(lock);return physical;
}
void SFCIOPCIDartMapper::stop(IOService *)
{
 // No false endpoint/IRQ/work-drain proof. Keep all mappings and ownership.
 if(lock){IOLockLock(lock);revoked=true;ready=false;IOLockUnlock(lock);}
 setProperty("quarantined-until-reboot",true);
}
void SFCIOPCIDartMapper::free()
{
 if(touched || mappingCount){IOLog("SFCIOPCIDartMapper: retaining SID0 tables/backing until reboot\n");return;}
 for(unsigned n=0;n<tableCount;++n)tables[n].memory->release();
 if(busy)IOFree(busy,pciPages);if(mappings)IOFree(mappings,sizeof(Mapping)*pciMappings);
 for(unsigned n=0;n<usbEndpointCount;++n)usbEndpoints[n]->release();
 if(lock)IOLockFree(lock);OSSafeReleaseNULL(endpoint);OSSafeReleaseNULL(controller);IOMapper::free();
}
