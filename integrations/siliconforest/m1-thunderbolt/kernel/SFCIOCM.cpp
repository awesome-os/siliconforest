// SPDX-License-Identifier: MIT
/* Actual IORegistry, reference-counted runtime policy and DART callbacks for
 * the private Linux connection manager. The cable lease pins controller power;
 * autosuspend hints are recorded, never reported as a hardware power-off.
 * All objects/resources live until reboot; failed drains retain ownership.
 */
#include "sf_cio_cm.h"
#include "sf_cio_dart.h"
#include "sf_tb_native_abi.h"
#include "sf_tb_pm.h"
#include "sf_tb_dma.h"
#include "sf_tb_nhi.h"
#include "sf_tb_retrain_policy.h"
#include "sf_rtkit_platform.h"
#ifdef SF_CIO_PCIE_ENUMERATION
#include "sf_cio_pci_enumeration.h"
#endif
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLocks.h>
#include <libkern/libkern.h>
#include <mach/mach_time.h>
#include <sys/errno.h>
extern "C" {
void sf_tb_native_log(const char *format,...)
{
 // A finite count bounds boot-time logging even if a real error repeats.
 static uint32_t count;
 if(!format)return;
 uint32_t previous=__atomic_load_n(&count,__ATOMIC_RELAXED);
 do {
  if(previous>=64)return;
 } while(!__atomic_compare_exchange_n(&count,&previous,previous+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
 char text[256];va_list args;va_start(args,format);
 vsnprintf(text,sizeof(text),format,args);va_end(args);
 IOLog("SFAppleCIO: CM: %s",text);
}
// XNU libkern/sysctl.h private KPI; present in the exact installed kernel.
int kernel_sysctlbyname(const char *,void *,size_t *,void *,size_t);
extern char hostname[]; // actual XNU export; no compatibility-layer homonym
int sf_cio_nhi_irqs_resolve(int [25]);
void sf_tb_native_fault(int);
}
struct SFCIOCM {
 IOService *owner;
 IORegistryEntry *entry;
 void *powerCtx,*device,*nhi;
 bool (*powered)(void *);
 IOLock *registryLock,*stateLock,*dmaLock;
 SFCIODart dart;
 int irqs[25];
 bool dmaLive,irqLive,busReady;
 uint32_t failed;
#ifdef SF_CIO_PCIE_ENUMERATION
 SFCIOPCIEnumeration *pci;
 static int pciPrepare(void *p){auto s=(SFCIOCM *)p;return s->pci?-s->pci->prepare():-ENODEV;}
 static int pciActivated(void *p){auto s=(SFCIOCM *)p;return s->pci?-s->pci->activated():-ENODEV;}
 static int pciDeactivate(void *p){auto s=(SFCIOCM *)p;return s->pci?-s->pci->deactivate():-ENODEV;}
#endif
 struct Slot {
  void *device;IOService *node;
  sf_tb_pm_state pm;
 } slots[128];
 Slot *slot(void *dev,bool create){
  for(auto &s:slots)if(s.device==dev)return &s;
  if(create)for(auto &s:slots)if(!s.device){s={};s.device=dev;sf_tb_pm_init(&s.pm);return &s;}
  return NULL;
 }
 bool live(){return sf_tb_native_ready() && !__atomic_load_n(&failed,__ATOMIC_ACQUIRE) && powered(powerCtx);}
 void fault(){__atomic_store_n(&failed,1,__ATOMIC_RELEASE);sf_tb_dma_quarantine();sf_tb_native_fault(-EIO);}
 static void lock(void *p){IOLockLock(((SFCIOCM *)p)->registryLock);}
 static void unlock(void *p){IOLockUnlock(((SFCIOCM *)p)->registryLock);}
 static void dma_lock(void *p){IOLockLock(((SFCIOCM *)p)->dmaLock);}
 static void dma_unlock(void *p){IOLockUnlock(((SFCIOCM *)p)->dmaLock);}
 static bool power(void *p){auto s=(SFCIOCM *)p;return s->live() && s->dart.healthy();}
 static int alloc(void *p,size_t bytes,size_t align,void **cpu,uint64_t *iova,void **token){
  auto s=(SFCIOCM *)p;
  if(!power(p))return -EIO;
  if(align<SF_DART_PAGE)align=SF_DART_PAGE;
  auto mem=SFCIODMABuffer::allocate(&s->dart,bytes,align);
  if(!mem)return s->dart.healthy()?-ENOMEM:-EIO;
  *cpu=mem->getBytesNoCopy();*iova=mem->dmaAddress();*token=mem;return 0;
 }
 static int retire(void *,void *token){
  auto mem=(SFCIODMABuffer *)token;int ret=mem->retire();
  if(ret)return -ret;
  mem->release();return 0;
 }
 static int osInfo(void *,void *out,size_t size){
  struct Uts {char fields[6][65];};
  if(!out || size!=sizeof(Uts))return -EINVAL;
  Uts v={};const char *names[]={"kern.ostype","kern.hostname","kern.osrelease","kern.osversion"};
  for(unsigned i=0;i<4;++i){
   // kern.hostname deliberately lacks CTLFLAG_KERN in XNU. A kernel sysctl
   // call returns EPERM even though userspace can read it. Use the actual
   // exported hostname, as Linux drivers read their live UTS string; reject
   // truncation. The other three MIBs explicitly allow kernel callers.
   if(i==1){
    size_t n=strnlen(hostname,sizeof(v.fields[i]));if(n==sizeof(v.fields[i]))return -EOVERFLOW;
    memcpy(v.fields[i],hostname,n+1);continue;
   }
   size_t n=sizeof(v.fields[i]);int ret=kernel_sysctlbyname(names[i],v.fields[i],&n,NULL,0);
   if(ret)return ret>0?-ret:ret;
   if(!n || n>sizeof(v.fields[i]) || !memchr(v.fields[i],0,n))return -EOVERFLOW;
  }
  // Hardware is qualified T8103/ARM64; no NIS domain is supplied by this CM.
  memcpy(v.fields[4],"arm64",6);
  memcpy(out,&v,sizeof(v));return 0;
 }
 static int publish(void *p,void *dev,void *parent,const char *name,const char *type,void **token){
  auto s=(SFCIOCM *)p;
  if(!dev || !token || !name || !*name || strnlen(name,128)==128 || !s->live())return -EINVAL;
  IOLockLock(s->stateLock);
  auto slot=s->slot(dev,true);auto par=s->slot(parent,false);
  IOService *provider=parent==s->device?s->owner:(par?par->node:NULL);
  bool ok=slot && !slot->node && provider;
  if(ok)provider->retain();IOLockUnlock(s->stateLock);
  if(!ok)return -ENODEV;
  auto node=new IOService;
  ok=node && node->init();
  if(ok){
   node->setName(name);
   ok=node->setProperty("thunderbolt-device-type",type?type:"device") &&
      node->setProperty("runtime-policy","cable-power-held") && node->attach(provider);
  }
  provider->release();
  if(!ok){OSSafeReleaseNULL(node);return -ENOMEM;}
  IOLockLock(s->stateLock);slot->node=node;IOLockUnlock(s->stateLock);
  node->registerService();*token=slot;return 0;
 }
 static void unpublish(void *p,void *token){
  auto s=(SFCIOCM *)p;auto slot=(Slot *)token;
  IOLockLock(s->stateLock);auto node=slot?slot->node:NULL;
  if(node)node->retain();IOLockUnlock(s->stateLock);
  if(!node){s->fault();return;}
  if(!node->terminate(kIOServiceRequired|kIOServiceSynchronous)){node->release();s->fault();return;}
  IOLockLock(s->stateLock);slot->node=NULL;IOLockUnlock(s->stateLock);
  if(node->getProvider())node->detach(node->getProvider());
  node->release();node->release();
 }
 static void forget(void *p,void *dev){
  auto s=(SFCIOCM *)p;IOLockLock(s->stateLock);auto slot=s->slot(dev,false);
  bool pending=slot && slot->node;
  if(slot && !pending)*slot={};
  IOLockUnlock(s->stateLock);if(pending)s->fault();
 }
 static int event(void *p,void *token,int action,const char *const *env,size_t count){
  auto s=(SFCIOCM *)p;auto slot=(Slot *)token;
  if(!slot || action<0 || action>2 || count>32)return -EINVAL;
  auto values=OSArray::withCapacity(count?count:1);if(!values)return -ENOMEM;
  bool ok=true;
  for(size_t i=0;i<count && ok;++i){
   if(!env || !env[i] || strnlen(env[i],2048)==2048){ok=false;break;}
   auto value=OSString::withCString(env[i]);ok=value && values->setObject(value);OSSafeReleaseNULL(value);
  }
  IOLockLock(s->stateLock);auto node=slot->node;if(node)node->retain();IOLockUnlock(s->stateLock);
  if(ok && node)ok=node->setProperty("thunderbolt-event",action,32) && node->setProperty("thunderbolt-event-values",values);
  else ok=false;
  OSSafeReleaseNULL(values);OSSafeReleaseNULL(node);return ok?0:-EIO;
 }
 static int runtime(void *p,void *dev,sf_tb_pm_op op,int arg){
  auto s=(SFCIOCM *)p;if(!dev || !s->live())return -EIO;
  IOLockLock(s->stateLock);auto v=s->slot(dev,true);int ret=0;
  if(!v){IOLockUnlock(s->stateLock);return -ENOMEM;}
  ret=sf_tb_pm_update(&v->pm,op,arg,v->node!=NULL,mach_absolute_time());
  IOLockUnlock(s->stateLock);return ret;
 }
 static int dartInterrupt(void *p){
  auto s=(SFCIOCM *)p;
  sf_rtkit_platform_irq_disable_nosync(s->irqs[24]);
  uint32_t raw=s->dart.faults();s->owner->setProperty("nhi-dart-fault",raw,32);
  s->fault();return 1; // retain backing and power for diagnosis, no retry loop
 }
};
SFCIOCM *sf_cio_cm_create(IOService *owner,void *ctx,bool (*powered)(void *))
{
 if(!owner)return NULL;
 auto failure=[owner](const char *stage,int error)->SFCIOCM *{
  owner->setProperty("cm-create-stage",stage);owner->setProperty("cm-create-error",(uint32_t)(error<0?-error:error),32);
  IOLog("SFAppleCIO: CM create %s failed errno=%d\n",stage,error);return NULL;
 };
 owner->setProperty("cm-create-stage","power");
 if(!powered || !powered(ctx))return failure("power",EIO);
 auto s=new SFCIOCM{};if(!s)return failure("context",ENOMEM);
 s->owner=owner;owner->retain();s->powerCtx=ctx;s->powered=powered;
 s->registryLock=IOLockAlloc();s->stateLock=IOLockAlloc();s->dmaLock=IOLockAlloc();
 s->entry=IORegistryEntry::fromPath("/arm-io/acio1",gIODTPlane);
 // Keep any partially installed callback context pinned until reboot.
 if(!s->registryLock || !s->stateLock || !s->dmaLock || !s->entry)return failure("resources",ENOMEM);
 sf_tb_native_ops ops={s,SFCIOCM::lock,SFCIOCM::unlock,SFCIOCM::publish,SFCIOCM::unpublish,SFCIOCM::event,SFCIOCM::runtime,SFCIOCM::osInfo,SFCIOCM::forget};
 OSObject *raw=s->entry->copyProperty("thunderbolt-drom");
 auto drom=OSDynamicCast(OSData,raw);
 int ret=drom && drom->getLength()==76?0:EINVAL;
 if(ret){OSSafeReleaseNULL(raw);return failure("drom",ret);}
 owner->setProperty("cm-create-stage","native-install");
 ret=sf_tb_native_install(&ops);
 if(ret){OSSafeReleaseNULL(raw);return failure("native-install",ret);}
 ret=sf_tb_nhi_device_create(s->entry,drom->getBytesNoCopy(),drom->getLength(),&s->device);
 OSSafeReleaseNULL(raw);if(ret)return failure("parent-device",ret);
 sf_tb_dma_ops dma={s,s->device,SFCIOCM::dma_lock,SFCIOCM::dma_unlock,SFCIOCM::power,SFCIOCM::alloc,SFCIOCM::retire};
 ret=sf_tb_dma_install(&dma);if(ret)return failure("dma-install",ret);
 ret=sf_tb_nhi_bus_init();if(ret)return failure("bus-init",ret);
 s->busReady=true;
#ifdef SF_CIO_PCIE_ENUMERATION
 s->pci=OSTypeAlloc(SFCIOPCIEnumeration);
 if(!s->pci || !s->pci->initWithOwner(owner,s,SFCIOCM::power))return failure("pci-enumeration-owner",ENOMEM);
 owner->setProperty("pci-enumeration-build",true);
 owner->setProperty("pci-enumeration-profile-valid",s->pci->checkProfile()==0);
#else
 owner->setProperty("pci-enumeration-build",false);
#endif
 owner->setProperty("cm-create-stage","complete");owner->setProperty("cm-create-error",(uint32_t)0,32);return s;
}
int sf_cio_cm_start(SFCIOCM *s,void *nhi,void *pdf,uint32_t cable)
{
 if(!s || !s->busReady || s->nhi || !s->live())return EINVAL;
 // Require pristine actual hardware before attaching/resetting its DART.
 auto base=(volatile uint32_t *)nhi;
 if(!base || !pdf || (base[0]&0x3ff)!=12 || base[0xd0010/4])return EBUSY;
 for(unsigned i=0;i<12;++i)if(base[(0x10010+i*0x4000)/4]&(1u<<31) || base[(0x80010+i*0x4000)/4]&(1u<<31))return EBUSY;
 s->owner->setProperty("cm-start-stage","dart-prepare");
 // prepare may change hardware before failing. Its poisoned/reset state
 // must participate in stop, even when no NHI descriptor was allocated.
 s->dmaLive=true;
 int ret=s->dart.prepare();if(ret){s->owner->setProperty("cm-start-error",(uint32_t)ret,32);return ret;}
 s->owner->setProperty("cm-start-stage","irq-resolve");
 ret=sf_cio_nhi_irqs_resolve(s->irqs);if(ret)return -ret;
 ret=sf_rtkit_platform_irq_request(s->irqs[24],SFCIOCM::dartInterrupt,s,"acio1-dart");if(ret)return -ret;
 s->irqLive=true;sf_rtkit_platform_irq_enable(s->irqs[24]);
 s->owner->setProperty("cm-start-stage","nhi-create");
 sf_tb_nhi_config config={};config.ctx=s;config.device=s->device;config.nhi_base=nhi;config.pdf_base=pdf;config.powered=SFCIOCM::power;
#ifdef SF_CIO_PCIE_ENUMERATION
 config.pci_prepare=SFCIOCM::pciPrepare;config.pci_activated=SFCIOCM::pciActivated;config.pci_deactivate=SFCIOCM::pciDeactivate;
#endif
 config.irq_request=sf_rtkit_platform_irq_request;config.irq_enable=sf_rtkit_platform_irq_enable;config.irq_free=sf_rtkit_platform_irq_free;
 for(unsigned i=0;i<12;++i){config.rx_irq[i]=s->irqs[i];config.tx_irq[i]=s->irqs[12+i];}
 ret=sf_tb_nhi_create(&config,&s->nhi);
 if(!ret){s->owner->setProperty("cm-start-stage","domain-start");ret=sf_tb_nhi_start(s->nhi,cable);}
 s->owner->setProperty("cm-start-error",(uint32_t)(ret<0?-ret:ret),32);
 if(!ret)s->owner->setProperty("cm-start-stage","complete");
 return ret<0?-ret:ret;
}
int sf_cio_cm_can_retrain(SFCIOCM *s)
{
#ifdef SF_CIO_PCIE_ENUMERATION
 if(!s || !s->live() || !s->nhi || !s->pci)return ENODEV;
 if(!s->pci->pristineForRetrain())return EBUSY;
 sf_tb_nhi_status actual={};int ret=sf_tb_nhi_snapshot(s->nhi,&actual);
 if(ret)return ret<0?-ret:ret;
 return sf_tb_retrain_peerless(&actual);
#else
 (void)s;return EOPNOTSUPP;
#endif
}
int sf_cio_cm_stop(SFCIOCM *s)
{
 if(!s)return EINVAL;
#ifdef SF_CIO_PCIE_ENUMERATION
 // tb_domain_remove() is void and cannot propagate a failed native tunnel
 // teardown. Do not mistake that return for a PCI child/DMA drain. This
 // first-enumeration profile has no such drain and pins power until reboot.
 if(s->pci && s->pci->blocksStop())return EBUSY;
#endif
 if(s->nhi){int ret=sf_tb_nhi_stop(s->nhi);if(ret)return ret<0?-ret:ret;s->nhi=NULL;}
 if(s->irqLive){sf_rtkit_platform_irq_free(s->irqs[24]);s->irqLive=false;}
 if(s->dmaLive){int ret=s->dart.suspend();if(ret)return ret;s->dmaLive=false;}
 return __atomic_load_n(&s->failed,__ATOMIC_ACQUIRE)?EIO:0;
}
int sf_cio_cm_authorize_ts3(SFCIOCM *s)
{
#ifdef SF_CIO_PCIE_ENUMERATION
 if(!s || !s->pci || !s->nhi || !s->live())return ENODEV;
 int ret=sf_tb_nhi_authorize_ts3(s->nhi);if(ret)return ret<0?-ret:ret;
 // Actual approval and post-activation have returned and released the
 // Linux domain lock. Native PCIe waits must never run inside those hooks.
 return s->pci->publishAfterApproval();
#else
 (void)s;return EOPNOTSUPP;
#endif
}
#ifdef SF_CIO_ENDPOINT_MAPPER
int sf_cio_cm_prepare_i210_dma(SFCIOCM *s)
{
 if(!s || !s->pci || !s->nhi || !s->live())return ENODEV;
 return s->pci->prepareI210Mapper();
}
#if SF_CIO_DOCK_USB
int sf_cio_cm_prepare_dock_usb(SFCIOCM *s)
{
 if(!s || !s->pci || !s->nhi || !s->live())return ENODEV;
 return s->pci->prepareDockUSBMapper();
}
#endif
#endif
int sf_cio_cm_snapshot(SFCIOCM *s)
{
 if(!s || !s->nhi || !s->live())return ENODEV;
 sf_tb_nhi_status status={};int ret=sf_tb_nhi_snapshot(s->nhi,&status);
 s->owner->setProperty("link-query-error",(uint64_t)(ret<0?-ret:ret),32);
 s->owner->setProperty("host-router-vendor",(uint64_t)status.root_vendor,32);
 s->owner->setProperty("host-router-device",(uint64_t)status.root_device,32);
 s->owner->setProperty("host-router-max-port",(uint64_t)status.max_port,32);
 s->owner->setProperty("nhi-cable-info-readback",(uint64_t)status.cable_info,32);
 bool headerOk=s->owner->setProperty("host-router-header",status.root_header,sizeof(status.root_header)) &&
  s->owner->setProperty("host-router-header-valid",status.root_header_valid!=0) &&
  s->owner->setProperty("nhi-irq-counts",status.irq_count,sizeof(status.irq_count));
 uint32_t controlOffsets[]={5,6,26};
 headerOk=headerOk &&
  s->owner->setProperty("host-router-control-offsets",controlOffsets,sizeof(controlOffsets)) &&
  s->owner->setProperty("host-router-controls",status.root_controls,sizeof(status.root_controls)) &&
  s->owner->setProperty("host-router-controls-valid",status.root_controls_valid!=0) &&
  s->owner->setProperty("host-router-controls-error",status.root_controls_error,32);
 auto ports=OSArray::withCapacity(status.port_count);
 if(!ports)return ENOMEM;
 bool ok=headerOk;
 for(unsigned i=0;i<status.port_count && ok;++i){
  const auto &p=status.ports[i];auto item=OSDictionary::withCapacity(17);
  if(!item){ok=false;break;}
  const char *keys[]={"port","state","error","remote","remote-vendor","remote-device",
   "phy-capability","flags-valid","lane-disabled","adapter-locked","remote-authorized",
   "usb4-capability","usb4-controls-valid","usb4-controls-error"};
  const uint32_t values[]={p.port,p.state,p.error,p.remote,p.remote_vendor,p.remote_device,
   p.cap_phy,p.flags_valid,p.lane_disabled,p.adapter_locked,p.remote_authorized,
   p.cap_usb4,p.usb4_controls_valid,p.usb4_controls_error};
  for(unsigned n=0;n<14 && ok;++n){auto v=OSNumber::withNumber((uint64_t)values[n],32);ok=v && item->setObject(keys[n],v);OSSafeReleaseNULL(v);}
  if(ok && p.flags_valid){
   auto phy=OSData::withBytes(p.phy,sizeof(p.phy));auto header=OSData::withBytes(p.header,sizeof(p.header));
   ok=phy && header && item->setObject("phy-registers",phy) && item->setObject("adapter-header",header);
   OSSafeReleaseNULL(phy);OSSafeReleaseNULL(header);
  }
  if(ok && p.usb4_controls_valid){
   auto value=OSData::withBytes(p.usb4_controls,sizeof(p.usb4_controls));
   ok=value && item->setObject("usb4-controls",value);OSSafeReleaseNULL(value);
  }
  if(ok)ok=ports->setObject(item);item->release();
 }
 if(ok)ok=s->owner->setProperty("physical-port-state",ports);
 ports->release();return !ok?ENOMEM:ret<0?-ret:ret;
}
