// SPDX-License-Identifier: MIT
#include "sf_cio_pci_enumeration.h"
#include <IOKit/IOCatalogue.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IORegistryEntry.h>
#include <libkern/c++/OSOrderedSet.h>
#include <sys/errno.h>
OSDefineMetaClassAndStructors(SFCIOPCIEnumeration,OSObject)
bool SFCIOPCIEnumeration::initWithOwner(IOService *service,void *ctx,bool (*proof)(void *))
{
 owner=nub=NULL;context=NULL;powerProof=NULL;lock=NULL;controller=NULL;host=NULL;
 posted=committed=attempted=revoked=0;preparing=false;
#ifdef SF_CIO_ENDPOINT_MAPPER
 i210Mapper=NULL;mapperAttempted=false;
#if SF_CIO_I210_NETWORK
 usbMapperEndpoint=NULL;usbMapperAttempted=false;
#endif
#endif
 if(!service || !ctx || !proof || !OSObject::init())return false;
 lock=IOLockAlloc();if(!lock)return false;
 service->retain();owner=service;context=ctx;powerProof=proof;return true;
}
bool SFCIOPCIEnumeration::power(void *p)
{
 auto s=(SFCIOPCIEnumeration *)p;
 return !__atomic_load_n(&s->revoked,__ATOMIC_ACQUIRE) && s->powerProof(s->context);
}
bool SFCIOPCIEnumeration::tunnel(void *p)
{
 auto s=(SFCIOPCIEnumeration *)p;
 return power(p) && __atomic_load_n(&s->posted,__ATOMIC_ACQUIRE) &&
        __atomic_load_n(&s->committed,__ATOMIC_ACQUIRE);
}
int SFCIOPCIEnumeration::catalogueSafe()
{
 if(!gIOCatalogue)return ENODEV;
 auto matching=OSDictionary::withCapacity(1);
 auto provider=OSString::withCString("IOPCIDevice");
 bool ok=matching && provider && matching->setObject("IOProviderClass",provider);
 OSSafeReleaseNULL(provider);if(!ok){OSSafeReleaseNULL(matching);return ENOMEM;}
 SInt32 generation=0;auto drivers=gIOCatalogue->findDrivers(matching,&generation);
 matching->release();
#if !defined(IOKIT_ENABLE_SHARED_PTR)
 struct ReleaseList { OSOrderedSet *value;~ReleaseList(){if(value)value->release();} } releaseList={drivers};
#endif
 if(!drivers || !drivers->getCount() || drivers->getCount()>64)return E2BIG;
 unsigned bridges=0,wifi=0;int ret=0;
#if SF_CIO_I210_NETWORK
 unsigned ethernet=0;
#endif
#if SF_CIO_DOCK_USB
 unsigned usb=0;
#endif
 for(unsigned i=0;i<drivers->getCount();++i){
  auto dict=OSDynamicCast(OSDictionary,drivers->getObject(i));
  auto cls=dict?OSDynamicCast(OSString,dict->getObject("IOClass")):NULL;
  auto bundle=dict?OSDynamicCast(OSString,dict->getObject("CFBundleIdentifier")):NULL;
  if(cls && cls->isEqualTo("IOPCI2PCIBridge") && bundle && bundle->isEqualTo("com.apple.iokit.IOPCIFamily")){++bridges;continue;}
  auto id=dict?OSDynamicCast(OSString,dict->getObject("IOPCIPrimaryMatch")):NULL;
  if(cls && cls->isEqualTo("SFBwfm") && bundle && bundle->isEqualTo("org.siliconforest.driver.SFBwfm") && id && id->isEqualTo("0x442514e4") &&
     !dict->getObject("IOPCIMatch") && !dict->getObject("IOPCIClassMatch")){++wifi;continue;}
#if SF_CIO_I210_NETWORK
  if(cls && cls->isEqualTo("SFFBSDem") && bundle && bundle->isEqualTo("org.siliconforest.driver.SFFBSDem") && id && id->isEqualTo("0x15338086") &&
     !dict->getObject("IOPCIMatch") && !dict->getObject("IOPCIClassMatch") && dict->getObject("IOPCITunnelCompatible")==kOSBooleanTrue){++ethernet;continue;}
#endif
#if SF_CIO_DOCK_USB
  if(cls && cls->isEqualTo("RavynXHCIPort") && bundle && bundle->isEqualTo("com.ravynos.driver.RavynXHCIPort") && id && id->isEqualTo("0x11001b73") &&
     !dict->getObject("IOPCIMatch") && !dict->getObject("IOPCIClassMatch") && dict->getObject("IOPCITunnelCompatible")==kOSBooleanTrue){++usb;continue;}
#endif
  ret=EOPNOTSUPP;break; // No generic endpoint personality, including PCI xHCI.
 }
 if(!ret && (!bridges || !wifi))ret=ENODEV;
#if SF_CIO_I210_NETWORK
 if(!ret && ethernet!=1)ret=ENODEV;
#endif
#if SF_CIO_DOCK_USB
 if(!ret && usb!=1)ret=ENODEV;
#endif
 owner->setProperty("pcie-catalogue-error",(uint32_t)ret,32);
 owner->setProperty("pcie-catalogue-count",drivers->getCount(),32);
 owner->setProperty("pcie-catalogue-generation",(uint32_t)generation,32);
 return ret;
}
int SFCIOPCIEnumeration::prepare()
{
 if(!power(this))return ENODEV;
 int ret=catalogueSafe();if(ret)return ret;
 IOLockLock(lock);
 if(preparing || controller || host || __atomic_load_n(&attempted,__ATOMIC_ACQUIRE)){
  IOLockUnlock(lock);return EBUSY;
 }
 preparing=true;IOLockUnlock(lock);
 auto ctl=OSTypeAlloc(SFCIOPCIController);
 bool ok=ctl && ctl->initWithOwner(owner,this,power,tunnel);
 IOLockLock(lock);preparing=false;if(ok)controller=ctl;IOLockUnlock(lock);
 if(!ok){OSSafeReleaseNULL(ctl);return ENODEV;}
 owner->setProperty("pcie-enumeration-stage","reserved-no-MMIO");return 0;
}
int SFCIOPCIEnumeration::checkProfile()
{return catalogueSafe();}
int SFCIOPCIEnumeration::activated()
{
 if(!power(this) || !controller || __atomic_load_n(&attempted,__ATOMIC_ACQUIRE))return ENODEV;
 // Real CM post-activation only. No CPU PCIe access or queued worker here.
 __atomic_store_n(&posted,1,__ATOMIC_RELEASE);
 owner->setProperty("pcie-enumeration-stage","post-callback-seen");return 0;
}
int SFCIOPCIEnumeration::fail(int error)
{
 if(host)host->blockConfiguration();
 owner->setProperty("pcie-enumeration-error",(uint32_t)error,32);
 owner->setProperty("pcie-enumeration-quarantined",true);
 owner->setProperty("pcie-enumeration-stage","quarantined-until-reboot");return error;
}
int SFCIOPCIEnumeration::publishAfterApproval()
{
 // The caller has returned from sf_tb_nhi_authorize_ts3(): both real
 // tb_domain_approve_switch and the post hook returned, domain lock released.
 if(!power(this) || !controller || !__atomic_load_n(&posted,__ATOMIC_ACQUIRE))return ENODEV;
 int ret=catalogueSafe();if(ret)return ret;
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&attempted,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return EBUSY;
 __atomic_store_n(&committed,1,__ATOMIC_RELEASE);
 owner->setProperty("pcie-enumeration-stage","cold-init");
 ret=controller->coldInit();if(ret)return fail(ret);
 owner->setProperty("pcie-enumeration-stage","link-start");
 ret=controller->linkStart();if(ret)return fail(ret);
 // Pinned pcie-apple.c APPLE_PCIE_TUNNEL_READY_MS: the TS3's FL1100 and
 // ASM1142 answer later than its I210/Alpine Ridge, behind switch ports
 // without hotplug capability. Give functions the specified reset-to-config
 // interval before the first native scan, or they can be missed forever.
 // No CM domain/controller lock is held; sleep, never spin or arm a timer.
 owner->setProperty("pcie-enumeration-stage","endpoint-reset-settle");
 IOSleep(1000);
 if(!tunnel(this))return fail(ENODEV);
 owner->setProperty("pcie-enumeration-stage","block-all-endpoint-DMA");
 // This backend freshly rechecks physical power/RUN/link before each DART
 // word; the sleep alone is not a claim that the real link stayed up.
 ret=controller->blockEndpointDMA();if(ret)return fail(ret);
 owner->setProperty("pcie-endpoint-settle-ms",(uint32_t)1000,32);
 ret=catalogueSafe();if(ret)return fail(ret);
 if(!tunnel(this))return fail(ENODEV);
 auto entry=IORegistryEntry::fromPath("/arm-io/apciec1",gIODTPlane);
 if(!entry)return fail(ENODEV);
 auto service=OSDynamicCast(IOService,entry);
 if(service){service->retain();nub=service;}
 else{
  auto made=OSTypeAlloc(IOPlatformDevice);
  bool ok=made && made->init(entry,gIODTPlane);
  if(ok)ok=made->attach(IOService::getPlatform());
  if(!ok){OSSafeReleaseNULL(made);entry->release();return fail(ENODEV);}
  nub=made;
 }
 entry->release();
 if(!nub->inPlane(gIOServicePlane) && !nub->attach(IOService::getPlatform()))return fail(ENODEV);
 if(!nub->setProperty("SFCIOPCIEnumerationOnly",true))return fail(ENOMEM);
 auto bridge=OSTypeAlloc(SFCIOPCIHost);
 if(!bridge || !bridge->initForEnumeration(owner,this,tunnel)){
  OSSafeReleaseNULL(bridge);return fail(ENOMEM);
 }
 // Publish the initialized pointer before attach/start, so concurrent CM
 // teardown can block configuration. Retain even partial start until reboot.
 IOLockLock(lock);host=bridge;IOLockUnlock(lock);
 owner->setProperty("pcie-enumeration-stage","bridge-start");
 if(!tunnel(this) || !host->attach(nub) || !host->start(nub))return fail(ENODEV);
 if(!tunnel(this))return fail(ENODEV);
 owner->setProperty("pcie-host-published",true);
 owner->setProperty("pcie-endpoint-DMA-admitted",false);
 owner->setProperty("pcie-enumeration-error",(uint32_t)0,32);
 owner->setProperty("pcie-enumeration-stage","host-published-inventory-required");return 0;
}
#ifdef SF_CIO_ENDPOINT_MAPPER
int SFCIOPCIEnumeration::prepareI210Mapper()
{
 // The owning CIO command gate serializes this with activation/revocation.
 // No Linux CM domain lock or controller lock may be held across start().
 if(!controller || !host || !tunnel(this) || host->isInactive())return ENODEV;
 int ret=catalogueSafe();if(ret)return ret; // Still no endpoint driver matching.
 IOLockLock(lock);
 bool prior=mapperAttempted;
 if(!prior)mapperAttempted=true;
 IOLockUnlock(lock);
 if(prior)return EBUSY;
 auto it=IORegistryIterator::iterateOver(host,gIOServicePlane,kIORegistryIterateRecursively);
 if(!it)return ENOMEM;
 IOPCIDevice *endpoint=NULL;unsigned seen=0;bool valid=true;
 while(auto object=it->getNextObject()){
  if(++seen>256){valid=false;break;}
  auto dev=OSDynamicCast(IOPCIDevice,object);if(!dev)continue;
  if(dev->configRead32(0)!=0x15338086 || (dev->configRead32(8)>>8)!=0x020000)continue;
  if(endpoint){valid=false;break;}
  endpoint=dev;endpoint->retain();
 }
 if(!it->isValid())valid=false;
 it->release();
 if(!valid || !endpoint){OSSafeReleaseNULL(endpoint);return ENODEV;}
 // Never replace another mapper binding, including an unresolved phandle.
 auto existing=endpoint->copyProperty("iommu-parent");
 if(existing){existing->release();endpoint->release();return EBUSY;}
 auto mapper=OSTypeAlloc(SFCIOPCIDartMapper);
 if(!mapper || !mapper->initWithController(controller,endpoint)){
  OSSafeReleaseNULL(mapper);endpoint->release();return ENOMEM;
 }
 // Publish the owned reference before any possible TTBR write. All partial
 // attempts stay pinned with this enumeration context until reboot.
 IOLockLock(lock);i210Mapper=mapper;IOLockUnlock(lock);
 owner->setProperty("pcie-i210-mapper-stage","start-and-validate-blocked-inventory");
 // initHardware freshly verifies four xHCI + one I210, all BME clear,
 // real host ancestry, requester/SID0, power/RUN/link and owned tables.
 bool ok=tunnel(this) && mapper->attach(endpoint) && mapper->start(endpoint);
 auto query=OSSymbol::withCString("SFCIOPCIDMAReady");
 if(ok)ok=query && mapper->callPlatformFunction(query,false,endpoint,NULL,NULL,NULL)==kIOReturnSuccess;
 OSSafeReleaseNULL(query);
 if(ok)ok=tunnel(this) && !endpoint->getProperty("iommu-parent") &&
           endpoint->setProperty("iommu-parent",mapper);
 if(ok){
  IOMapper *resolved=IOMapper::copyMapperForDevice(endpoint);
  ok=resolved==mapper;OSSafeReleaseNULL(resolved);
 }
 query=OSSymbol::withCString("SFCIOPCIDMAReady");
 if(ok)ok=query && tunnel(this) &&
   mapper->callPlatformFunction(query,false,endpoint,NULL,NULL,NULL)==kIOReturnSuccess;
 OSSafeReleaseNULL(query);
 if(!ok){
  mapper->stop(endpoint); // Revoke/pin only, never a DMA drain acknowledgment.
  endpoint->release();return fail(EIO);
 }
 mapper->registerService();
 owner->setProperty("pcie-i210-mapper-stage","actual-object-bound-no-BME");
 owner->setProperty("pcie-i210-mapper-created",true); // Historical binding, not a live/DMA proof.
#if SF_CIO_I210_NETWORK
 // Initial matching refused before the mapper existed. Retry the exact
 // endpoint only after binding and fresh native readiness checks above.
 int resources=host->publishI210Resources(endpoint);
 if(resources){endpoint->release();return fail(resources);}
 if(host->bindI210Access(endpoint)){endpoint->release();return fail(ENODEV);}
 endpoint->registerService(kIOServiceSynchronous);
 owner->setProperty("pcie-i210-mapper-stage","mapper-bound-network-match-requested");
#endif
 endpoint->release();return 0;
}
#if SF_CIO_I210_NETWORK
int SFCIOPCIEnumeration::prepareDockUSBMapper()
{
 // Explicit owner command gate only, after the real I210 is active.
 // Only the dedicated dock profile retries matching after all bindings.
 if(!i210Mapper || !host || !controller || !tunnel(this) || host->isInactive())return ENODEV;
 auto ethernet=OSDynamicCast(IOPCIDevice,i210Mapper->getProvider());
 if(!ethernet || ethernet->configRead32(0)!=0x15338086 ||
    !(ethernet->configRead16(4)&4))return ENODEV;
 IOMapper *bound=IOMapper::copyMapperForDevice(ethernet);bool same=bound==i210Mapper;OSSafeReleaseNULL(bound);
 if(!same)return ENODEV;
 int ret=catalogueSafe();if(ret)return ret;
 IOLockLock(lock);bool prior=usbMapperAttempted;usbMapperAttempted=true;IOLockUnlock(lock);
 if(prior)return EBUSY;
 auto it=IORegistryIterator::iterateOver(host,gIOServicePlane,kIORegistryIterateRecursively);
 if(!it)return ENOMEM;
 IOPCIDevice *endpoint=NULL;unsigned seen=0;bool valid=true;
 while(auto object=it->getNextObject()){
  if(++seen>256){valid=false;break;}
  auto dev=OSDynamicCast(IOPCIDevice,object);
  if(!dev || dev->configRead32(0)!=0x11001b73 || (dev->configRead32(8)>>8)!=0x0c0330)continue;
  // Linux's attached Keychron is on the FL1100 behind switch port0. Darwin
  // bus numbering differs: derive the nearest actual Alpine Ridge bridge.
  IORegistryEntry *parent=dev->getParentEntry(gIOServicePlane);IOPCIDevice *bridge=NULL;unsigned depth=0;
  while(parent && parent!=host && depth++<64){
   bridge=OSDynamicCast(IOPCIDevice,parent);if(bridge)break;
   parent=parent->getParentEntry(gIOServicePlane);
  }
  if(!bridge || bridge->configRead32(0)!=0x15d38086 ||
     (bridge->configRead32(8)>>8)!=0x060400 || bridge->getDeviceNumber() || bridge->getFunctionNumber())continue;
  if(endpoint){valid=false;break;}endpoint=dev;endpoint->retain();
 }
 if(!it->isValid())valid=false;it->release();
 if(!valid || !endpoint){OSSafeReleaseNULL(endpoint);return ENODEV;}
 // Keep even partial requester/binding state with its real owner until
 // reboot. Registration validates live root ownership and never resets it.
 usbMapperEndpoint=endpoint;
 IOReturn registered=i210Mapper->registerDockUSB(endpoint);
 if(registered!=kIOReturnSuccess)return fail(EIO);
 if(!tunnel(this) || endpoint->getProperty("iommu-parent") ||
    !endpoint->setProperty("iommu-parent",i210Mapper))return fail(EIO);
 bound=IOMapper::copyMapperForDevice(endpoint);same=bound==i210Mapper;OSSafeReleaseNULL(bound);
 auto query=OSSymbol::withCString("SFCIOPCIDMAReady");
 bool ready=same && query && i210Mapper->callPlatformFunction(query,false,endpoint,NULL,NULL,NULL)==kIOReturnSuccess;
 OSSafeReleaseNULL(query);if(!ready)return fail(EIO);
 ret=host->publishDockUSBResources(endpoint);if(ret)return fail(ret);
 ret=host->resolveDockUSBInterrupt(endpoint);if(ret)return fail(ret);
 ret=host->bindDockUSBAccess(endpoint);if(ret)return fail(ret);
 owner->setProperty("pcie-usb-mapper-stage","actual-shared-root-resources-MSI-no-driver-no-BME");
 owner->setProperty("pcie-usb-mapper-created",true);
#if SF_CIO_DOCK_USB
 endpoint->registerService(kIOServiceSynchronous);
 owner->setProperty("pcie-usb-mapper-stage","mapper-bound-FL1100-match-requested");
#endif
 return 0;
}
#endif
#endif
int SFCIOPCIEnumeration::deactivate()
{
 __atomic_store_n(&revoked,1,__ATOMIC_RELEASE);
 __atomic_store_n(&committed,0,__ATOMIC_RELEASE);
 __atomic_store_n(&posted,0,__ATOMIC_RELEASE);
 IOLockLock(lock);auto bridge=host;auto ctl=controller;
#ifdef SF_CIO_ENDPOINT_MAPPER
 auto mapper=i210Mapper;if(mapper)mapper->retain();
#endif
 IOLockUnlock(lock);
#ifdef SF_CIO_ENDPOINT_MAPPER
 if(mapper){mapper->stop(mapper->getProvider());mapper->release();} // Revoke/pin, no drain.
#endif
 if(bridge)bridge->blockConfiguration();
 int ret=ctl?ctl->revoke():0;
 // No synchronous child termination while the CM domain lock is held.
 // A programmed first-enumeration profile pins all ownership until reboot.
 if(bridge && !ret)ret=EBUSY;
 if(ret)return fail(ret);
 owner->setProperty("pcie-enumeration-stage","revoked-before-programming");return 0;
}
bool SFCIOPCIEnumeration::blocksStop() const
{return __atomic_load_n(&attempted,__ATOMIC_ACQUIRE)!=0;}
bool SFCIOPCIEnumeration::pristineForRetrain()
{
 if(!lock)return false;
 IOLockLock(lock);
 bool ok=!preparing && !controller && !host && !nub &&
  !__atomic_load_n(&attempted,__ATOMIC_ACQUIRE) &&
  !__atomic_load_n(&posted,__ATOMIC_ACQUIRE) &&
  !__atomic_load_n(&revoked,__ATOMIC_ACQUIRE);
 IOLockUnlock(lock);return ok;
}
void SFCIOPCIEnumeration::free()
{
 if(controller || host || nub)return; // pinned experimental context, no drain claim
 if(lock)IOLockFree(lock);OSSafeReleaseNULL(owner);OSObject::free();
}
