// SPDX-License-Identifier: GPL-2.0
/* T8103 port-1 ACIO owner. Startup order/registers from Linux apple.c and
 * reset-apple-cio.c, Copyright (c) Sven Peter <sven@kernel.org> and
 * Copyright The Asahi Linux Contributors. No firmware or calibration shipped.
 * Firmware-ready is distinct from NHI/domain/PCI readiness.
 */
#include "sf_soc.h"
#ifdef SF_CIO_CM
#include "sf_cio_cm.h"
#include "sf_cd321x.h"
#endif
#include "sf_cio_rtkit.h"
#include "sf_cio_tunables.h"
#include "sf_usbc_phy_lease.h"
#include "sf_rtkit_platform.h"
#include "sf_fbsd_os.h"
#include "sf_fbsd_xnu.h"
#include <IOKit/IOService.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOUserClient.h>
#include <pexpert/pexpert.h>
int sf_cio_power_off();
int sf_cio_pcie_reset();
#define LOG(fmt,...) IOLog("SFAppleCIO: " fmt "\n",##__VA_ARGS__)
class SFAppleCIO: public IOService {
 OSDeclareDefaultStructors(SFAppleCIO)
 IOWorkLoop *loop;
 IOCommandGate *gate;
 IOTimerEventSource *timer;
 SFMmio rc,nhi,pdf,adapter,sram,reset;
 OSDictionary *tunableData;
 IORegistryEntry *cpuEntry;
 sf_cio_tunables tunables;
 sf_cio_tunable_io tuning;
 sf_cio_rtkit *rtkit;
 uint64_t leaseToken,attemptedEpoch;
 uint64_t drainToken,revokeToken;
 uint32_t accepting,drained;
 uint32_t firmwareStarts;
 bool power,rtkitTouched,firmwareReady,quarantined,stopping;
#ifdef SF_CIO_CM
 SFCIOCM *cm;
 uint32_t cmLive;
 IOTimerEventSource *diagnosticTimer;
 uint32_t diagnosticCount;
 uint32_t authorizationCount;
 uint32_t retrainCount;
#ifdef SF_CIO_ENDPOINT_MAPPER
 uint32_t mapperCount;
 static IOReturn mapperAction(OSObject *,void *,void *,void *,void *);
#if SF_CIO_DOCK_USB
 uint32_t usbMapperCount;
 static IOReturn usbMapperAction(OSObject *,void *,void *,void *,void *);
#endif
#endif
 static void diagnosticTick(OSObject *,IOTimerEventSource *);
 static IOReturn snapshotAction(OSObject *,void *,void *,void *,void *);
 static IOReturn authorizationAction(OSObject *,void *,void *,void *,void *);
 static IOReturn retrainAction(OSObject *,void *,void *,void *,void *);
 int readSnapshot();
 static bool cmPowered(void *);
 int startCM();
#endif
 int mapResources();
 int preparePCIe();
 int deassertReset();
 int startFirmware(sf_usbc_phy_request &);
 int stopFirmware();
 void step();
 void report(int);
 static void tick(OSObject *,IOTimerEventSource *);
 static IOReturn stopAction(OSObject *,void *,void *,void *,void *);
 static const void *getprop(void *,const char *,const char *,size_t *);
 static uint32_t read(void *,sf_cio_window,uint32_t);
 static void write(void *,sf_cio_window,uint32_t,uint32_t);
 SFMmio &window(sf_cio_window w){return w==SF_CIO_RC?rc:w==SF_CIO_NHI?nhi:adapter;}
 sf_usbc_phy_request request(unsigned command){
  sf_usbc_phy_request r={};r.version=SF_USBC_PHY_ABI;r.size=sizeof(r);
  r.command=command;r.port=1;r.owner=this;r.token=leaseToken;return r;
 }
 int phy(sf_usbc_phy_request &r){
  IOReturn ret=getProvider()->callPlatformFunction(SF_USBC_PHY_FUNCTION,false,&r,NULL,NULL,NULL);
  return r.error?r.error:(ret==kIOReturnSuccess?0:EIO);
 }
public:
 IOService *probe(IOService *,SInt32 *) override;
 bool start(IOService *) override;
 void stop(IOService *) override;
 void free() override;
 IOReturn message(UInt32,IOService *,void *) override;
 IOReturn setProperties(OSObject *) override;
 IOReturn callPlatformFunction(const OSSymbol *,bool,void *,void *,void *,void *) override;
};
OSDefineMetaClassAndStructors(SFAppleCIO,IOService)
const void *SFAppleCIO::getprop(void *ctx,const char *path,const char *name,size_t *len)
{
 auto s=(SFAppleCIO *)ctx;*len=0;
 if(strcmp(path,"/arm-io/acio1") || !s->tunableData)return NULL;
 auto data=OSDynamicCast(OSData,s->tunableData->getObject(name));
 if(!data){
  auto entry=IORegistryEntry::fromPath(path,gIODTPlane);
  OSObject *v=entry?entry->copyProperty(name):NULL;
  auto source=OSDynamicCast(OSData,v);
  auto copy=source?OSData::withBytes(source->getBytesNoCopy(),source->getLength()):NULL;
  bool ok=copy && s->tunableData->setObject(name,copy);
  OSSafeReleaseNULL(copy);OSSafeReleaseNULL(v);OSSafeReleaseNULL(entry);
  if(!ok)return NULL;
  data=OSDynamicCast(OSData,s->tunableData->getObject(name));
 }
 *len=data->getLength();return data->getBytesNoCopy();
}
uint32_t SFAppleCIO::read(void *ctx,sf_cio_window w,uint32_t off){return ((SFAppleCIO *)ctx)->window(w).r32(off);}
void SFAppleCIO::write(void *ctx,sf_cio_window w,uint32_t off,uint32_t v){((SFAppleCIO *)ctx)->window(w).w32(off,v);}
int SFAppleCIO::mapResources()
{
 if(rtkit)return 0; // Immutable validated resources/context retained for this boot.
 setProperty("resource-stage","acio-reg-layout");
 OSSafeReleaseNULL(cpuEntry);OSSafeReleaseNULL(tunableData);
 SFMmio rc1,rc2;
 if(!nhi.mapNode("/arm-io/acio1",0) || nhi.phys!=0x501f00000ULL || nhi.len<0x100000 ||
    !pdf.mapNode("/arm-io/acio1",1) || pdf.phys!=0x501db0000ULL || pdf.len<0x30004 ||
    !rc.mapNode("/arm-io/acio1",2) || rc.phys!=0x501ac0000ULL || rc.len!=0x4000 ||
    !rc1.mapNode("/arm-io/acio1",3) || rc1.phys!=rc.phys+0x4000 || rc1.len!=0x4000 ||
    !rc2.mapNode("/arm-io/acio1",4) || rc2.phys!=rc.phys+0x8000 || rc2.len!=0x4000 ||
    !adapter.mapNode("/arm-io/acio1",5) || adapter.phys!=0x501e44000ULL || adapter.len<0x4000){
  rc1.unmap();rc2.unmap();return ENODEV;
 }
 rc1.unmap();rc2.unmap();rc.unmap();
 if(!rc.mapPhys(0x501ac0000ULL,0xc000))return ENOMEM;
 setProperty("resource-stage","m3-compatible");
 cpuEntry=IORegistryEntry::fromPath("/arm-io/acio-cpu1",gIODTPlane);
 auto compatible=OSString::withCString("iop,m3wrap-v2-acio");
 bool cpuMatch=cpuEntry && compatible && IODTCompareNubName(cpuEntry,compatible,NULL);
 OSSafeReleaseNULL(compatible);
 if(!cpuMatch)return ENODEV;
 setProperty("resource-stage","sram-segment");
 auto nub=IORegistryEntry::fromPath("/arm-io/acio-cpu1/iop-acio1-nub",gIODTPlane);
 auto segments=nub?OSDynamicCast(OSData,nub->getProperty("segment-ranges")):NULL;
 auto names=nub?OSDynamicCast(OSData,nub->getProperty("segment-names")):NULL;
 bool valid=false;
 // m1n1 ADT: phys, iova, remap, u32 size, u32 flags. Require the exact
 // qualified T8103 SRAM segment and exclude firmware text. Never read text.
 if(segments && segments->getLength()==64 && names && names->getLength()==14 &&
    !memcmp(names->getBytesNoCopy(),"__TEXT;__DATA",14)){
  const uint8_t *d=(const uint8_t *)segments->getBytesNoCopy()+32;
  uint64_t pa=OSReadLittleInt64(d,0),iova=OSReadLittleInt64(d,8),remap=OSReadLittleInt64(d,16);
  uint32_t span=OSReadLittleInt32(d,24),flags=OSReadLittleInt32(d,28);
  valid=pa==0x501080000ULL && iova==0x10000000 && remap==iova && span==0x20000 && !flags && sram.mapPhys(pa,span);
 }
 OSSafeReleaseNULL(nub);
 if(!valid)return EINVAL;
 setProperty("resource-stage","pmgr-reset-window");
 if(!reset.mapNode("/arm-io/pmgr",0) || reset.phys!=0x23b700000ULL || reset.len<0x88004)return ENODEV;
 setProperty("resource-stage","tunables");
 tunableData=OSDictionary::withCapacity(11);
 if(!tunableData)return ENOMEM;
 tuning={this,getprop,read,write};
 int ret=sf_cio_tunables_load(&tunables,&tuning);
 if(ret)return ret;
 // Never introduce the RTKit compatibility layer's 2 ms polling mode.
 if(sf_rtkit_platform_bootarg("sfrtkit_irq",2)!=2)return EOPNOTSUPP;
 setProperty("resource-stage","mailbox-platform");
 ret=sf_rtkit_platform_init(this);
 if(ret)return ret<0?-ret:ret;
 sf_rtkit_platform_set_log_prefix("SFAppleCIO: rtkit: ");
 rtkit=sf_cio_rtkit_create(cpuEntry,(void *)sram.va,sram.phys,sram.len);
 if(rtkit)setProperty("resource-stage","complete");
 return rtkit?0:ENOMEM;
}
int SFAppleCIO::preparePCIe()
{
 auto node=IORegistryEntry::fromPath("/arm-io/apciec1",gIODTPlane);
 auto dart=IORegistryEntry::fromPath("/arm-io/dart-apciec1",gIODTPlane);
 if(!node || !dart){OSSafeReleaseNULL(node);OSSafeReleaseNULL(dart);return ENODEV;}
 auto clients=node->getChildIterator(gIOServicePlane);
 auto dmaClients=dart->getChildIterator(gIOServicePlane);
 bool bound=(clients && clients->getNextObject()) || (dmaClients && dmaClients->getNextObject());
 OSSafeReleaseNULL(clients);OSSafeReleaseNULL(dmaClients);OSSafeReleaseNULL(node);OSSafeReleaseNULL(dart);
 if(bound)return EBUSY;
 int ret=sf_pmgr_enable_node("/arm-io/apciec1",-1,0,true);
 return ret?ret:sf_cio_pcie_reset();
}
int SFAppleCIO::deassertReset()
{
 // Port 1 only, Linux reset-apple-cio.c: bounded pre-poll is advisory;
 // write INIT_REQ then require exactly INIT_DONE within 100 ms.
 for(unsigned i=0;i<1000 && (reset.r32(0x88000)&2);++i)IODelay(100);
 reset.w32(0x88000,1);
 for(unsigned i=0;i<1000;++i){if(reset.r32(0x88000)==4)return 0;IODelay(100);}
 return ETIMEDOUT;
}
int SFAppleCIO::startFirmware(sf_usbc_phy_request &q)
{
 if(firmwareStarts==UINT32_MAX)return EOVERFLOW;
 int ret=mapResources();if(ret)return ret;
 q.command=SF_USBC_PHY_ACQUIRE;q.mode=(q.eligible&SF_USBC_PHY_MODE_BIT(SF_USBC_PHY_TBT))?SF_USBC_PHY_TBT:SF_USBC_PHY_USB4;
 ret=phy(q);
 // A failed PHY setup can still retain this client's lease when cleanup
 // failed. Keep that token and drain identity even on an error response.
 leaseToken=q.token;
 if(!ret && !leaseToken)ret=EPROTO;
 __atomic_store_n(&drainToken,leaseToken,__ATOMIC_RELEASE);
 __atomic_store_n(&drained,0,__ATOMIC_RELEASE);
 if(ret)return ret;
 ret=preparePCIe();if(ret)return ret;
 power=true; // Also unwind a partially failed enable.
 ret=sf_pmgr_enable_node("/arm-io/acio1",-1,0,true);if(ret)return ret;
 ret=deassertReset();if(ret)return ret;
 uint32_t before=rc.r32(0x0c);
 rc.mask32(0x0c,0,2); // preserve undocumented iBoot bits; request M3 startup
 uint32_t after=rc.r32(0x0c);++firmwareStarts;
 rtkitTouched=true;
 ret=sf_cio_rtkit_boot(rtkit);if(ret)return ret<0?-ret:ret;
 for(unsigned i=0;i<5000;++i){
  uint32_t state=rc.r32(0xa8);
  if(state!=UINT32_MAX && (state&0x7f000000)){
   setProperty("firmware-start-count",firmwareStarts,32);
   setProperty("m3-before-start",before,32);setProperty("m3-after-start",after,32);
   if(firmwareStarts==1){setProperty("m3-initial-before-start",before,32);setProperty("m3-initial-after-start",after,32);}
   firmwareReady=true;setProperty("m3-firmware-state",state,32);
   ret=sf_cio_tunables_apply(&tunables,&tuning,SF_CIO_RC);
   if(!ret)ret=sf_cio_tunables_apply(&tunables,&tuning,SF_CIO_PCIE_ADAPTER);
   if(ret)return ret;
   setProperty("nhi-hop-count",nhi.r32(0)&0x3ff,32);
   setProperty("nhi-interrupt-enable",nhi.r32(0xd0010),32);
   setProperty("rtkit-ready",true);
   // NHI/domain attachment is a separate, still absent stage. No ring/DART
   // is programmed and no PCI device is published by this firmware stage.
   setProperty("nhi-ready",false);
#ifdef SF_CIO_CM
   if(sf_rtkit_platform_bootarg("sfcio_cm",0)==1){
    __atomic_store_n(&cmLive,1,__ATOMIC_RELEASE);
    ret=startCM();if(ret)return ret;
   }
#endif
   return 0;
  }
  IODelay(100);
 }
 return ETIMEDOUT;
}
int SFAppleCIO::stopFirmware()
{
#ifdef SF_CIO_CM
 if(diagnosticTimer)diagnosticTimer->cancelTimeout();
 if(cm){int ret=sf_cio_cm_stop(cm);if(ret)return ret;}
 if(power && firmwareReady){
  setProperty("nhi-interrupt-enable",nhi.r32(0xd0010),32);
  setProperty("nhi-control-tx-options",nhi.r32(0x10010),32);
  setProperty("nhi-control-rx-options",nhi.r32(0x80010),32);
 }
 __atomic_store_n(&cmLive,0,__ATOMIC_RELEASE);setProperty("nhi-ready",false);
#endif
 firmwareReady=false;setProperty("rtkit-ready",false);
 if(rtkitTouched){
  int ret=sf_cio_rtkit_stop(rtkit);if(ret)return ret<0?-ret:ret;
  rtkitTouched=false;
 }
 if(power){int ret=sf_cio_power_off();if(ret)return ret;power=false;}
 __atomic_store_n(&drained,1,__ATOMIC_RELEASE);
 if(leaseToken){
  auto q=request(SF_USBC_PHY_RELEASE);int ret=phy(q);if(ret)return ret;
  leaseToken=0;__atomic_store_n(&drainToken,0,__ATOMIC_RELEASE);
 }
 return 0;
}
void SFAppleCIO::report(int ret){setProperty("last-error",(uint32_t)ret,32);setProperty("phy-owned",leaseToken!=0);setProperty("quarantined",quarantined);if(ret)LOG("port1 stage error=%d, PHY retained=%d",ret,leaseToken!=0);}
void SFAppleCIO::step()
{
 if(stopping || quarantined)return;
 auto q=request(SF_USBC_PHY_QUERY);int ret=phy(q);
 if(leaseToken){
  if(q.phase==SF_USBC_PHY_REVOKED || __atomic_load_n(&revokeToken,__ATOMIC_ACQUIRE)==leaseToken){
   ret=stopFirmware();if(ret)quarantined=true;report(ret);
   if(!ret)timer->setTimeoutMS(1);
  }
  return;
 }
 if(ret){report(ret);return;}
 if(q.epoch==attemptedEpoch || !(q.eligible&(SF_USBC_PHY_MODE_BIT(SF_USBC_PHY_TBT)|SF_USBC_PHY_MODE_BIT(SF_USBC_PHY_USB4))))return;
 attemptedEpoch=q.epoch;
 ret=startFirmware(q);
 if(ret){int cleanup=stopFirmware();if(cleanup)quarantined=true;}
 report(ret);
}
void SFAppleCIO::tick(OSObject *owner,IOTimerEventSource *){auto s=OSDynamicCast(SFAppleCIO,owner);if(s)s->step();}
IOService *SFAppleCIO::probe(IOService *p,SInt32 *score)
{
 int enabled=0;uint32_t chip=0;
 if(!PE_parse_boot_argn("sfcio",&enabled,sizeof(enabled)) || enabled!=1 ||
    !sf_adt_u32("/chosen","chip-id",0,&chip) || chip!=0x8103)return NULL;
 return IOService::probe(p,score);
}
bool SFAppleCIO::start(IOService *p)
{
 if(!IOService::start(p))return false;
 sfos_xnu_init();if(sf_cio_layer_up())return false;
 loop=IOWorkLoop::workLoop();gate=IOCommandGate::commandGate(this);timer=IOTimerEventSource::timerEventSource(this,tick);
 if(!loop || !gate || !timer || loop->addEventSource(gate)!=kIOReturnSuccess || loop->addEventSource(timer)!=kIOReturnSuccess)return false;
#ifdef SF_CIO_CM
 diagnosticTimer=IOTimerEventSource::timerEventSource(this,diagnosticTick);
 if(!diagnosticTimer || loop->addEventSource(diagnosticTimer)!=kIOReturnSuccess)return false;
 authorizationCount=retrainCount=0;setProperty("authorization-request-count",(uint32_t)0,32);
#ifdef SF_CIO_ENDPOINT_MAPPER
 mapperCount=0;setProperty("i210-mapper-request-count",(uint32_t)0,32);
#if SF_CIO_DOCK_USB
 usbMapperCount=0;setProperty("dock-usb-mapper-request-count",(uint32_t)0,32);
#endif
#endif
 setProperty("retrain-request-count",(uint32_t)0,32);
#endif
 firmwareStarts=0;setProperty("firmware-start-count",(uint32_t)0,32);
 setProperty("port-index",1,32);setProperty("rtkit-ready",false);setProperty("nhi-ready",false);
 __atomic_store_n(&accepting,1,__ATOMIC_RELEASE);__atomic_store_n(&drained,1,__ATOMIC_RELEASE);
 registerService();timer->setTimeoutMS(1);return true;
}
IOReturn SFAppleCIO::setProperties(OSObject *properties)
{
#ifdef SF_CIO_CM
 // Same actual calling-task privilege check as IOPCIFamily. This endpoint
 // accepts exact snapshot/TS3 approval/one peerless retrain commands, never
 // arbitrary addresses, cable words or register values.
 if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
 auto dict=OSDynamicCast(OSDictionary,properties);
 if(!dict || dict->getCount()!=1)return kIOReturnBadArgument;
 if(!gate || !__atomic_load_n(&accepting,__ATOMIC_ACQUIRE))return kIOReturnNotReady;
 if(dict->getObject("sf-cio-read-snapshot")==kOSBooleanTrue)return gate->runAction(snapshotAction);
 if(dict->getObject("sf-cio-authorize-ts3")==kOSBooleanTrue)return gate->runAction(authorizationAction);
#ifdef SF_CIO_ENDPOINT_MAPPER
 if(dict->getObject("sf-cio-prepare-i210-mapper")==kOSBooleanTrue)return gate->runAction(mapperAction);
#if SF_CIO_DOCK_USB
 if(dict->getObject("sf-cio-prepare-dock-usb")==kOSBooleanTrue)return gate->runAction(usbMapperAction);
#endif
#endif
 if(dict->getObject("sf-cio-retrain-peerless")==kOSBooleanTrue)return gate->runAction(retrainAction);
 return kIOReturnBadArgument;
#else
 (void)properties;return kIOReturnUnsupported;
#endif
}
IOReturn SFAppleCIO::message(UInt32 type,IOService *p,void *a)
{
 if(type==kIOMessageServicePropertyChange && p==getProvider()){
  if(__atomic_load_n(&accepting,__ATOMIC_ACQUIRE) && timer)timer->setTimeoutMS(1);
  return kIOReturnSuccess;
 }
 return IOService::message(type,p,a);
}
IOReturn SFAppleCIO::callPlatformFunction(const OSSymbol *name,bool wait,void *a,void *b,void *c,void *d)
{
 if(name && name->isEqualTo(SF_USBC_PHY_DRAIN_FUNCTION)){
  return a && __atomic_load_n(&drained,__ATOMIC_ACQUIRE) && *(uint64_t *)a==__atomic_load_n(&drainToken,__ATOMIC_ACQUIRE)?kIOReturnSuccess:kIOReturnBusy;
 }
 if(name && name->isEqualTo(SF_USBC_PHY_REVOKE_FUNCTION)){
  if(!a)return kIOReturnBadArgument;
  __atomic_store_n(&revokeToken,*(uint64_t *)a,__ATOMIC_RELEASE);
  if(__atomic_load_n(&accepting,__ATOMIC_ACQUIRE) && timer)timer->setTimeoutMS(1);
  return kIOReturnSuccess;
 }
 return IOService::callPlatformFunction(name,wait,a,b,c,d);
}
IOReturn SFAppleCIO::stopAction(OSObject *owner,void *,void *,void *,void *)
{
 auto s=OSDynamicCast(SFAppleCIO,owner);if(!s)return kIOReturnBadArgument;
 s->stopping=true;int ret=s->stopFirmware();
 if(ret){s->quarantined=true;s->retain();}s->report(ret);
 return ret?kIOReturnBusy:kIOReturnSuccess;
}
void SFAppleCIO::stop(IOService *p)
{
 __atomic_store_n(&accepting,0,__ATOMIC_RELEASE);
 if(timer){timer->cancelTimeout();timer->disable();if(loop)loop->removeEventSource(timer);}
#ifdef SF_CIO_CM
 if(diagnosticTimer){diagnosticTimer->cancelTimeout();diagnosticTimer->disable();if(loop)loop->removeEventSource(diagnosticTimer);}
#endif
 if(gate)gate->runAction(stopAction);else stopAction(this,NULL,NULL,NULL,NULL);
 IOService::stop(p);
}
void SFAppleCIO::free()
{
 if(leaseToken || power || rtkitTouched || quarantined){LOG("retaining failed-drain resources");return;}
 if(loop && gate)loop->removeEventSource(gate);
#ifdef SF_CIO_CM
 if(loop && diagnosticTimer && diagnosticTimer->getWorkLoop()==loop)loop->removeEventSource(diagnosticTimer);
 OSSafeReleaseNULL(diagnosticTimer);
#endif
 OSSafeReleaseNULL(timer);OSSafeReleaseNULL(gate);OSSafeReleaseNULL(loop);
 OSSafeReleaseNULL(tunableData);OSSafeReleaseNULL(cpuEntry);
 rc.unmap();nhi.unmap();pdf.unmap();adapter.unmap();sram.unmap();reset.unmap();IOService::free();
}

#ifdef SF_CIO_CM
int SFAppleCIO::readSnapshot()
{
 if(stopping || quarantined || !cm || !cmPowered(this))return ENODEV;
 if(diagnosticCount==UINT32_MAX)return EOVERFLOW;
 ++diagnosticCount;setProperty("link-query-count",(uint64_t)diagnosticCount,32);
 setProperty("m3-control-readback",rc.r32(0x0c),32);
 setProperty("m3-live-state",rc.r32(0xa8),32);
 setProperty("nhi-interrupt-enable-live",nhi.r32(0xd0010),32);
 setProperty("nhi-interrupt-status-live",nhi.r32(0xd0000),32);
 sf_cio_rtkit_status transport={};int transportRet=sf_cio_rtkit_snapshot(rtkit,&transport);
 setProperty("rtkit-transport-error",(uint32_t)(transportRet<0?-transportRet:transportRet),32);
 setProperty("rtkit-transport-valid",transportRet==0);
 if(!transportRet){
  setProperty("rtkit-protocol",transport.protocol,32);
  setProperty("rtkit-ap-power",transport.ap_power,32);setProperty("rtkit-iop-power",transport.iop_power,32);
  setProperty("rtkit-running",transport.running!=0);setProperty("rtkit-crashed",transport.crashed!=0);
  setProperty("rtkit-shutting-down",transport.shutting_down!=0);
  setProperty("rtkit-syslog-entries",transport.syslog_entries,32);
  setProperty("rtkit-syslog-message-bytes",transport.syslog_message_bytes,32);
  setProperty("rtkit-syslog-buffer-bytes",transport.syslog_buffer_bytes,32);
  setProperty("rtkit-syslog-mapped",transport.syslog_mapped!=0);
  setProperty("rtkit-syslog-ack-count",transport.syslog_ack_count,32);
  setProperty("rtkit-syslog-ack-errors",transport.syslog_ack_errors,32);
  setProperty("rtkit-syslog-first-ack-error",transport.syslog_first_ack_error,32);
  setProperty("rtkit-ioreport-ack-count",transport.ioreport_ack_count,32);
  setProperty("rtkit-ioreport-ack-errors",transport.ioreport_ack_errors,32);
  setProperty("rtkit-ioreport-first-ack-error",transport.ioreport_first_ack_error,32);
  setProperty("rtkit-mailbox-active",transport.mailbox_active!=0);
  setProperty("rtkit-mailbox-a2i-control",transport.a2i_control,32);
  setProperty("rtkit-mailbox-i2a-control",transport.i2a_control,32);
  setProperty("rtkit-mailbox-irq-enable",transport.mailbox_irq_enable,32);
  setProperty("rtkit-mailbox-irq-state",transport.mailbox_irq_state,sizeof(transport.mailbox_irq_state));
 }
 auto req=request(SF_USBC_PHY_SNAPSHOT);int phyRet=phy(req);
 setProperty("phy-query-error",(uint64_t)phyRet,32);
 auto typec=getProvider()?getProvider()->getProvider():NULL;
 IOReturn control=typec?typec->callPlatformFunction("SFAppleTypeCControlSnapshot",false,NULL,NULL,NULL,NULL):kIOReturnNotReady;
 setProperty("typec-control-query-error",(uint32_t)control,32);
 const size_t capacity=512,bytes=capacity*sizeof(sf_cio_tunable_observation);size_t count=0;
 auto records=(sf_cio_tunable_observation *)IOMallocData(bytes);
 int tuneRet=records?sf_cio_tunables_observe(&tunables,&tuning,records,capacity,&count):ENOMEM;
 if(!tuneRet && !setProperty("cio-tuning-registers",records,count*sizeof(*records)))tuneRet=ENOMEM;
 if(records)IOFreeData(records,bytes);
 setProperty("cio-tuning-count",(uint32_t)count,32);
 setProperty("cio-tuning-error",(uint32_t)tuneRet,32);
 setProperty("cio-tuning-valid",tuneRet==0);
 int ret=sf_cio_cm_snapshot(cm);
 if(!ret && transportRet)ret=transportRet<0?-transportRet:transportRet;
 setProperty("link-query-error",(uint64_t)ret,32);
 return phyRet?phyRet:ret?ret:tuneRet?tuneRet:control==kIOReturnSuccess?0:EIO;
}
void SFAppleCIO::diagnosticTick(OSObject *owner,IOTimerEventSource *)
{
 auto s=OSDynamicCast(SFAppleCIO,owner);if(s)s->readSnapshot();
 // No rearm: one startup query, plus explicit privileged requests below.
}
IOReturn SFAppleCIO::snapshotAction(OSObject *owner,void *,void *,void *,void *)
{
 auto s=OSDynamicCast(SFAppleCIO,owner);
 if(!s || !__atomic_load_n(&s->accepting,__ATOMIC_ACQUIRE) || s->stopping || s->quarantined || !s->cm || !cmPowered(s))return kIOReturnNotReady;
 return s->readSnapshot()?kIOReturnIOError:kIOReturnSuccess;
}
IOReturn SFAppleCIO::authorizationAction(OSObject *owner,void *,void *,void *,void *)
{
 auto s=OSDynamicCast(SFAppleCIO,owner);
 if(!s || !__atomic_load_n(&s->accepting,__ATOMIC_ACQUIRE) || s->stopping || s->quarantined || !s->cm || !cmPowered(s))return kIOReturnNotReady;
 if(s->authorizationCount==UINT32_MAX)return kIOReturnNoResources;
 s->setProperty("authorization-request-count",++s->authorizationCount,32);
 int ret=sf_cio_cm_authorize_ts3(s->cm);
 s->setProperty("authorization-request-error",(uint32_t)ret,32);
 return !ret?kIOReturnSuccess:ret==EOPNOTSUPP?kIOReturnUnsupported:kIOReturnIOError;
}
#ifdef SF_CIO_ENDPOINT_MAPPER
IOReturn SFAppleCIO::mapperAction(OSObject *owner,void *,void *,void *,void *)
{
 auto s=OSDynamicCast(SFAppleCIO,owner);
 if(!s || !__atomic_load_n(&s->accepting,__ATOMIC_ACQUIRE) || s->stopping || s->quarantined || !s->cm || !cmPowered(s))return kIOReturnNotReady;
 if(s->mapperCount)return kIOReturnNotPermitted;
 s->setProperty("i210-mapper-request-count",++s->mapperCount,32);
 int ret=sf_cio_cm_prepare_i210_dma(s->cm);
 s->setProperty("i210-mapper-request-error",(uint32_t)ret,32);
 return ret?kIOReturnIOError:kIOReturnSuccess;
}
#if SF_CIO_DOCK_USB
IOReturn SFAppleCIO::usbMapperAction(OSObject *owner,void *,void *,void *,void *)
{
 auto s=OSDynamicCast(SFAppleCIO,owner);
 if(!s || !__atomic_load_n(&s->accepting,__ATOMIC_ACQUIRE) || s->stopping || s->quarantined || !s->cm || !cmPowered(s))return kIOReturnNotReady;
 if(s->usbMapperCount)return kIOReturnNotPermitted;
 s->setProperty("dock-usb-mapper-request-count",++s->usbMapperCount,32);
 int ret=sf_cio_cm_prepare_dock_usb(s->cm);
 s->setProperty("dock-usb-mapper-request-error",(uint32_t)ret,32);
 return ret?kIOReturnIOError:kIOReturnSuccess;
}
#endif
#endif
IOReturn SFAppleCIO::retrainAction(OSObject *owner,void *,void *,void *,void *)
{
 auto s=OSDynamicCast(SFAppleCIO,owner);
 if(!s || !__atomic_load_n(&s->accepting,__ATOMIC_ACQUIRE) || s->stopping || s->quarantined ||
    !s->cm || !s->leaseToken || !cmPowered(s))return kIOReturnNotReady;
 if(s->retrainCount)return kIOReturnNoResources; // One explicit attempt per boot.
 int ret=sf_cio_cm_can_retrain(s->cm);
 auto q=s->request(SF_USBC_PHY_QUERY);
 if(!ret)ret=s->phy(q);
 // QUERY returns phase/eligibility/token; mode is an ACQUIRE input, not a
 // QUERY output. A live CM has already validated its actual TBT cable word.
 if(!ret && (q.phase!=SF_USBC_PHY_LIVE ||
    !(q.eligible&SF_USBC_PHY_MODE_BIT(SF_USBC_PHY_TBT))))ret=EBUSY;
 s->setProperty("retrain-precheck-error",(uint32_t)ret,32);
 if(ret)return ret==EOPNOTSUPP?kIOReturnUnsupported:kIOReturnNotReady;
 uint64_t epoch=q.epoch;
 s->setProperty("retrain-request-count",++s->retrainCount,32);
 s->setProperty("retrain-stage","stop-drain-poweroff-release");
 // Actual Linux domain/ring/worker/IRQ/DART drain, then RTKit poweroff,
 // physical ACIO power-off and broker-verified PHY release, in that order.
 ret=s->stopFirmware();
 if(ret){s->quarantined=true;s->report(ret);s->setProperty("retrain-error",(uint32_t)ret,32);return kIOReturnIOError;}
 s->setProperty("retrain-stop-complete",true);
 q=s->request(SF_USBC_PHY_QUERY);ret=s->phy(q);
 if(!ret && (q.epoch!=epoch || !(q.eligible&SF_USBC_PHY_MODE_BIT(SF_USBC_PHY_TBT))))ret=ESTALE;
 if(!ret){
  s->attemptedEpoch=q.epoch; // No automatic retry of this same cable epoch.
  s->setProperty("retrain-stage","reacquire-phy-and-start");ret=s->startFirmware(q);
 }
 if(ret){int cleanup=s->stopFirmware();if(cleanup)s->quarantined=true;}
 s->report(ret);s->setProperty("retrain-error",(uint32_t)ret,32);
 s->setProperty("retrain-stage",ret?"failed-no-retry":"restart-complete");
 return ret?kIOReturnIOError:kIOReturnSuccess;
}
bool SFAppleCIO::cmPowered(void *ctx)
{
 auto s=(SFAppleCIO *)ctx;
 if(!__atomic_load_n(&s->cmLive,__ATOMIC_ACQUIRE))return false;
 // Actual ATC-CIO, AON-CIO and AUSB-CIO power states, plus NHI accessibility.
 const unsigned gates[]={0x458,0x460,0x468};
 for(unsigned off:gates)if((s->reset.r32(off)&0xf0)!=0xf0)return false;
 return (s->nhi.r32(0)&0x3ff)==12;
}
int SFAppleCIO::startCM()
{
 auto typec=getProvider()?getProvider()->getProvider():NULL;
 OSObject *value=typec?typec->copyProperty("port-state"):NULL;
 auto data=OSDynamicCast(OSData,value);sf_cd321x_cable_snapshot snapshot={};uint32_t cable=0;
 int ret=data?sf_cd321x_snapshot_decode(data->getBytesNoCopy(),data->getLength(),&snapshot):ENODEV;
 if(!ret)ret=sf_cd321x_tbt_cable_info(&snapshot,&cable);
 OSSafeReleaseNULL(value);if(ret)return ret;
 // USB4 EUDO support is a separate stage. Never guess a TBT cable word.
 setProperty("cm-atc-cio-power",reset.r32(0x458),32);
 setProperty("cm-aon-cio-power",reset.r32(0x460),32);
 setProperty("cm-ausb-cio-power",reset.r32(0x468),32);
 if(!cm)cm=sf_cio_cm_create(this,this,cmPowered);
 if(!cm)return EIO;
 // Linux apple_nhi_probe applies NHI fabric/interrupt-management tunables
 // before the control rings can run. The firmware-only stage intentionally
 // did not touch them. Require the real, exclusive controller to be pristine
 // before any NHI write; the native ring owner repeats this check on entry.
 if((nhi.r32(0)&0x3ff)!=12 || nhi.r32(0xd0010))return EBUSY;
 for(unsigned i=0;i<12;++i){
  if((nhi.r32(0x10010+i*0x4000)|(nhi.r32(0x80010+i*0x4000)))&(1u<<31))return EBUSY;
 }
 ret=sf_cio_tunables_apply(&tunables,&tuning,SF_CIO_NHI);
 setProperty("nhi-start-fabric-applied",ret==0);if(ret)return ret;
 ret=sf_cio_cm_start(cm,(void *)nhi.va,(void *)pdf.va,cable);
 setProperty("nhi-cable-info",cable,32);setProperty("nhi-ready",ret==0);
 setProperty("nhi-interrupt-enable",nhi.r32(0xd0010),32);
 setProperty("nhi-control-tx-options",nhi.r32(0x10010),32);
 setProperty("nhi-control-rx-options",nhi.r32(0x80010),32);
 if(!ret && diagnosticTimer)diagnosticTimer->setTimeoutMS(1000);
 return ret;
}
#endif
