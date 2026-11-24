// SPDX-License-Identifier: GPL-2.0
// Real per-port IOPCIFamily MSI controller -> AIC parent vectors.
#include "sf_cio_pci_msi.h"
#include "sf_soc.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMapper.h>
#include <machine/machine_routines.h>
OSDefineMetaClassAndStructors(SFCIOPCIMSIController,IOPCIMessagedInterruptController)
bool SFCIOPCIMSIController::initForPort(IOService *owner,void *ctx,bool (*proof)(void *),
 UInt32 vectorBase,UInt32 vectorCount,UInt32 address)
{
 powerOwner=NULL;loop=NULL;powerContext=NULL;powerProof=NULL;accepting=failed=0;pinned=false;
 bzero(parents,sizeof(parents));base=vectorBase;count=vectorCount;doorbell=address;
 uint32_t chip=0,dtBase=0;
 if(!owner || !owner->metaCast("SFCIOPCIHost") || !ctx || !proof || !proof(ctx) ||
    !sf_adt_u32("/chosen","chip-id",0,&chip) || chip!=0x8103 ||
    !sf_adt_u32("/arm-io/apciec1","msi-vector-offset",0,&dtBase) ||
    dtBase!=818 || base!=dtBase || count!=32 || doorbell!=0xfffff000)return false;
 owner->retain();powerOwner=owner;powerContext=ctx;powerProof=proof;
 // The real family allocator supplies indexes flagged PCIMessaged. Its
 // distinct subclass name gives this controller a unique registry name.
 if(!IOPCIMessagedInterruptController::init(count,base))return false;
 retain();pinned=true; // Parent handlers and any partial setup live until reboot.
 loop=IOWorkLoop::workLoop();if(!loop){fault();return false;}
 for(UInt32 n=0;n<count;++n){
  auto &p=parents[n];p.owner=this;p.slot=n;
  auto nub=OSTypeAlloc(IOService);
  if(!nub || !nub->init()){OSSafeReleaseNULL(nub);fault();return false;}
  p.nub=nub;nub->setName("SFCIOPCIeC-MSI-parent");
  IOInterruptVectorNumber vector=base+n;
  const OSSymbol *aic=gIODTDefaultInterruptController;
  auto spec=OSData::withBytes(&vector,sizeof(vector));
  auto controllers=aic?OSArray::withObjects((const OSObject **)&aic,1):NULL;
  auto specs=spec?OSArray::withObjects((const OSObject **)&spec,1):NULL;
  bool ok=controllers && specs && nub->setProperty(gIOInterruptControllersKey,controllers) &&
   nub->setProperty(gIOInterruptSpecifiersKey,specs) && nub->attach(this);
  OSSafeReleaseNULL(spec);OSSafeReleaseNULL(controllers);OSSafeReleaseNULL(specs);
  if(!ok){fault();return false;}
  p.soft=IOInterruptEventSource::interruptEventSource(this,rearm,NULL,0);
  if(!p.soft || loop->addEventSource(p.soft)!=kIOReturnSuccess){fault();return false;}
  p.soft->enable();
  IOReturn error=nub->registerInterrupt(0,this,primaryInterrupt,&p);
  if(error!=kIOReturnSuccess){fault();return false;}
  p.registered=true;
  if(nub->disableInterrupt(0)!=kIOReturnSuccess){fault();return false;}
 }
 __atomic_store_n(&accepting,1,__ATOMIC_RELEASE);
 setProperty("msi-vector-base",base,32);setProperty("msi-vector-count",count,32);
 setProperty("endpoint-DMA-admitted",false);return ready();
}
bool SFCIOPCIMSIController::ready()
{
 return __atomic_load_n(&accepting,__ATOMIC_ACQUIRE) &&
  !__atomic_load_n(&failed,__ATOMIC_ACQUIRE) && powerProof && powerProof(powerContext);
}
void SFCIOPCIMSIController::fault()
{__atomic_store_n(&failed,1,__ATOMIC_RELEASE);__atomic_store_n(&accepting,0,__ATOMIC_RELEASE);}
void SFCIOPCIMSIController::revoke()
{__atomic_store_n(&accepting,0,__ATOMIC_RELEASE);}
bool SFCIOPCIMSIController::deviceAllowed(IOPCIDevice *device)
{
 if(!device || ml_at_interrupt_context() || !ready())return false;
 IORegistryEntry *p=device;unsigned depth=0;
 while(p && p!=powerOwner && depth++<64)p=p->getParentEntry(gIOServicePlane);
 if(p!=powerOwner)return false;
 UInt32 id=device->configRead32(0),code=device->configRead32(8)>>8;
 bool i210=id==0x15338086 && code==0x020000;
 bool usb=code==0x0c0330 && (id==0x11001b73 || id==0x12421b21 || id==0x15d48086);
 // USB gets no MSI until the explicit factory bound this actual endpoint
 // to the live shared domain. Bridge hotplug and generic PCI stay refused.
 if(!i210 && !(usb && mapperReady(device)))return false;
 UInt8 cap=0;return device->findPCICapability(kIOPCIMSICapability,&cap)!=0 && cap!=0;
}
bool SFCIOPCIMSIController::mapperReady(IOPCIDevice *device)
{
 auto mapper=IOMapper::copyMapperForDevice(device);
 if(!mapper)return false;
 bool ok=mapper->metaCast("SFCIOPCIDartMapper") &&
  mapper->callPlatformFunction("SFCIOPCIDMAReady",false,device,NULL,NULL,NULL)==kIOReturnSuccess;
#if !defined(IOKIT_ENABLE_SHARED_PTR)
 mapper->release();
#endif
 return ok;
}
uint32_t SFCIOPCIMSIController::getDeviceMSILimit(IOPCIDevice *device,uint32_t requested)
{
 // One native MSI for I210 or an explicitly bound real dock xHCI endpoint.
 return requested && deviceAllowed(device)?1:0;
}
IOReturn SFCIOPCIMSIController::messageForDevice(IOService *entry,UInt32 vector,UInt32 *message)
{
 auto device=OSDynamicCast(IOPCIDevice,entry);
 if(!message || vector<base || vector-base>=count || !deviceAllowed(device))return kIOReturnUnsupported;
 message[0]=doorbell;message[1]=0;message[2]=vector-base;return kIOReturnSuccess;
}
IOReturn SFCIOPCIMSIController::registerInterrupt(IOService *nub,int source,
 void *target,IOInterruptHandler handler,void *refCon)
{
 auto device=OSDynamicCast(IOPCIDevice,nub);
 if(!deviceAllowed(device) || !mapperReady(device))return kIOReturnNotReady;
 IOReturn error=IOPCIMessagedInterruptController::registerInterrupt(nub,source,target,handler,refCon);
 if(error!=kIOReturnSuccess)return error;
 UInt8 cap=0;
 if(!device->findPCICapability(kIOPCIMSICapability,&cap) || !cap ||
    !(device->configRead16(cap+2)&1)){
  IOPCIMessagedInterruptController::unregisterInterrupt(nub,source);fault();return kIOReturnIOError;
 }
 return kIOReturnSuccess;
}
void SFCIOPCIMSIController::primaryInterrupt(OSObject *,void *refCon,IOService *,int)
{
 auto p=(Parent *)refCon;auto s=p->owner;
 // This frozen AIC acknowledges and masks on event read, without setting
 // interruptDisabledHard. As in the real RTKit transport, re-register on
 // a workloop after the primary handler returns to obtain a real unmask.
 p->nub->disableInterrupt(0);
 __atomic_store_n(&p->armed,0,__ATOMIC_RELEASE);
 __atomic_store_n(&p->delivered,1,__ATOMIC_RELEASE);
 __atomic_add_fetch(&p->fired,1,__ATOMIC_RELAXED);
 if(!s->ready()){
  // No PCIe access or blocking child teardown in the parent interrupt.
  p->nub->disableInterrupt(0);__atomic_store_n(&p->enabled,0,__ATOMIC_RELEASE);s->fault();return;
 }
 if(!__atomic_load_n(&p->enabled,__ATOMIC_ACQUIRE))return;
 // The actual family's dispatcher takes a global vector and invokes the
 // registered IOKit handler/filter, including its normal active accounting.
 s->handleInterrupt(NULL,p->nub,s->base+p->slot);
 p->soft->interruptOccurred(NULL,NULL,0);
}
void SFCIOPCIMSIController::rearm(OSObject *owner,IOInterruptEventSource *sender,int)
{
 auto s=OSDynamicCast(SFCIOPCIMSIController,owner);if(!s)return;
 for(UInt32 n=0;n<s->count;++n){
  auto &p=s->parents[n];if(p.soft!=sender)continue;
  if(!s->ready() || !__atomic_load_n(&p.enabled,__ATOMIC_ACQUIRE))return;
  if(__atomic_exchange_n(&p.delivered,0,__ATOMIC_ACQ_REL)){
   // Thread context; unregister joins the actual AIC primary callback.
   if(p.nub->unregisterInterrupt(0)!=kIOReturnSuccess){s->fault();return;}
   p.registered=false;
   if(p.nub->registerInterrupt(0,s,primaryInterrupt,&p)!=kIOReturnSuccess){s->fault();return;}
   p.registered=true;
  }
  if(!s->ready() || !__atomic_load_n(&p.enabled,__ATOMIC_ACQUIRE))return;
  // Publish before the real unmask: an immediately delivered interrupt
  // clears this fact again rather than being overwritten on return.
  __atomic_store_n(&p.armed,1,__ATOMIC_RELEASE);
  if(p.nub->enableInterrupt(0)!=kIOReturnSuccess){
   __atomic_store_n(&p.armed,0,__ATOMIC_RELEASE);s->fault();return;
  }
  if(!s->ready() || !__atomic_load_n(&p.enabled,__ATOMIC_ACQUIRE)){
   p.nub->disableInterrupt(0);__atomic_store_n(&p.armed,0,__ATOMIC_RELEASE);
  }
  return;
 }
}
void SFCIOPCIMSIController::enableVector(IOInterruptVectorNumber n,IOInterruptVector *vector)
{
 if(n<0 || (UInt32)n>=count || !ready())return;
 IOPCIMessagedInterruptController::enableVector(n,vector);
 __atomic_store_n(&parents[n].enabled,1,__ATOMIC_RELEASE);
 parents[n].soft->interruptOccurred(NULL,NULL,0);
}
void SFCIOPCIMSIController::disableVectorHard(IOInterruptVectorNumber n,IOInterruptVector *vector)
{
 if(n<0 || (UInt32)n>=count)return;
 __atomic_store_n(&parents[n].enabled,0,__ATOMIC_RELEASE);
 __atomic_store_n(&parents[n].armed,0,__ATOMIC_RELEASE);
 if(parents[n].nub && parents[n].nub->disableInterrupt(0)!=kIOReturnSuccess)fault();
 IOPCIMessagedInterruptController::disableVectorHard(n,vector);
}
bool SFCIOPCIMSIController::routeReady(IOPCIDevice *device)
{
 if(!deviceAllowed(device) || !mapperReady(device))return false;
 for(UInt32 n=0;n<count;++n){
  auto v=&vectors[n];IOLockLock(v->interruptLock);
  bool owns=v->interruptRegistered && v->nub==device &&
   __atomic_load_n(&parents[n].enabled,__ATOMIC_ACQUIRE) &&
   __atomic_load_n(&parents[n].armed,__ATOMIC_ACQUIRE);
  IOLockUnlock(v->interruptLock);if(owns)return true;
 }
 return false;
}
void SFCIOPCIMSIController::free()
{
 if(pinned){IOLog("SFCIOPCIMSIController: retaining AIC parents until real endpoint/IRQ/work drain\n");return;}
 OSSafeReleaseNULL(powerOwner);IOPCIMessagedInterruptController::free();
}
