// SPDX-License-Identifier: GPL-2.0
// Actual I210/ancestor admission, separate from the inventory-only profile.
#include "sf_cio_pcie_host.h"
#include "sf_pciec_bar.h"
#include <IOKit/IOMapper.h>
#include <IOKit/IORegistryEntry.h>
#include <machine/machine_routines.h>
#include <libkern/OSByteOrder.h>
#include <sys/errno.h>

int SFCIOPCIHost::bindI210Access(IOPCIDevice *device)
{
    if(!device || ml_at_interrupt_context() || !interruptPower(this) ||
       device->configRead32(0)!=0x15338086 || (device->configRead32(8)>>8)!=0x020000)return ENODEV;
    IORegistryEntry *p=device;unsigned depth=0;
    while(p && p!=this && depth++<64)p=p->getParentEntry(gIOServicePlane);
    if(p!=this || networkEndpoint)return EBUSY;
    device->retain();__atomic_store_n(&networkEndpoint,device,__ATOMIC_RELEASE);
    return 0;
}

int SFCIOPCIHost::bindDockUSBAccess(IOPCIDevice *device)
{
    if(!device || ml_at_interrupt_context() || !interruptPower(this) ||
       device->configRead32(0)!=0x11001b73 || (device->configRead32(8)>>8)!=0x0c0330 ||
       (device->configRead16(4)&4))return ENODEV;
    IORegistryEntry *p=device;unsigned depth=0;
    while(p && p!=this && depth++<64)p=p->getParentEntry(gIOServicePlane);
    if(p!=this || usbEndpoint)return EBUSY;
    auto mapper=IOMapper::copyMapperForDevice(device);
    bool ready=mapper && mapper->metaCast("SFCIOPCIDartMapper") &&
        mapper->getProvider()==networkEndpoint &&
        mapper->callPlatformFunction("SFCIOPCIDMAReady",false,device,NULL,NULL,NULL)==kIOReturnSuccess;
    OSSafeReleaseNULL(mapper);if(!ready)return ENODEV;
    device->retain();__atomic_store_n(&usbEndpoint,device,__ATOMIC_RELEASE);
    return 0;
}

IOReturn SFCIOPCIHost::admitDockUSB(IOPCIDevice *device)
{
    if(!device || device!=__atomic_load_n(&usbEndpoint,__ATOMIC_ACQUIRE) ||
       ml_at_interrupt_context() || !msi || !interruptPower(this) ||
       device->configRead32(0)!=0x11001b73 || (device->configRead32(8)>>8)!=0x0c0330 ||
       (device->configRead16(4)&4))return kIOReturnNotReady;
    auto mapper=IOMapper::copyMapperForDevice(device);
    bool ready=mapper && mapper->metaCast("SFCIOPCIDartMapper") &&
        mapper->getProvider()==networkEndpoint &&
        mapper->callPlatformFunction("SFCIOPCIDMAReady",false,device,NULL,NULL,NULL)==kIOReturnSuccess;
    OSSafeReleaseNULL(mapper);
    if(!ready || !msi->routeReady(device))return kIOReturnNotReady;
    Grant chain[8]={};UInt32 count=0;unsigned depth=0;
    IORegistryEntry *p=device;
    while(p && p!=this && depth++<64){
        auto pci=OSDynamicCast(IOPCIDevice,p);
        if(pci){
            if(count==8)return kIOReturnUnsupported;
            UInt32 id=pci->configRead32(0),code=pci->configRead32(8)>>8;
            if(id==UINT32_MAX || (!count ? pci!=device || code!=0x0c0330 : code!=0x060400))
                return kIOReturnUnsupported;
            chain[count++]={pci,id,code,pci->getBusNumber(),pci->getDeviceNumber(),pci->getFunctionNumber()};
        }
        p=p->getParentEntry(gIOServicePlane);
    }
    if(p!=this || count<2)return kIOReturnUnsupported;
    // Retained identities from I210 admission are the only existing grants.
    // Snapshot under the config lock, then use normal config calls outside it.
    Grant old[16]={};UInt32 oldCount=0;
    IOLockLock(accessLock);
    bool active=view.admitted && bmeActive && networkAttempted && !usbAttempted;
    if(active){oldCount=grantCount;memcpy(old,grants,oldCount*sizeof(*old));}
    IOLockUnlock(accessLock);if(!active || !oldCount || oldCount>8)return kIOReturnNotPermitted;
    auto it=IORegistryIterator::iterateOver(this,gIOServicePlane,kIORegistryIterateRecursively);
    if(!it)return kIOReturnNoMemory;
    unsigned seen=0,xhci=0,i210=0;bool valid=true;
    while(auto object=it->getNextObject()){
        if(++seen>256){valid=false;break;}
        auto pci=OSDynamicCast(IOPCIDevice,object);if(!pci)continue;
        UInt32 id=pci->configRead32(0),code=pci->configRead32(8)>>8;
        UInt16 command=pci->configRead16(4);bool granted=false;
        for(UInt32 n=0;n<oldCount;++n)if(old[n].device==pci){
            granted=old[n].id==id && old[n].code==code &&
                old[n].bus==pci->getBusNumber() && old[n].slot==pci->getDeviceNumber() &&
                old[n].function==pci->getFunctionNumber();
            if(!granted)valid=false;
        }
        if(id==UINT32_MAX || command==UINT16_MAX || bool(command&4)!=granted){valid=false;break;}
        if(code==0x0c0330 && (id==0x11001b73 || id==0x12421b21 || id==0x15d48086))++xhci;
        else if(pci==networkEndpoint && id==0x15338086 && code==0x020000)++i210;
        else if(code!=0x060400){valid=false;break;}
    }
    if(!it->isValid())valid=false;it->release();
    if(!valid || xhci!=4 || i210!=1 || !interruptPower(this) || !msi->routeReady(device))return kIOReturnNotReady;
    IOLockLock(accessLock);
    if(!view.admitted || !bmeActive || usbAttempted || grantCount!=oldCount){IOLockUnlock(accessLock);return kIOReturnNotPermitted;}
    usbAttempted=true;
    for(UInt32 n=0;n<count;++n){
        bool found=false;
        for(UInt32 i=0;i<grantCount;++i)if(grants[i].device==chain[n].device)found=true;
        if(!found){grants[grantCount]=chain[n];grants[grantCount++].device->retain();}
    }
    IOLockUnlock(accessLock); // <= old8 + new8; existing I210 grants unchanged.
    bool ok=true;
    for(UInt32 n=count;n>0;--n){
        if(!interruptPower(this)){ok=false;break;}
        auto pci=chain[n-1].device;pci->setBusMasterEnable(true);
        UInt16 command=pci->configRead16(4);
        if(command==UINT16_MAX || !(command&4)){ok=false;break;}
    }
    if(!ok){
        leaseOwner->setProperty("pcie-usb-BME-error",(UInt32)EIO,32);
        blockConfiguration();return kIOReturnIOError; // Pin all, no false drain.
    }
    setProperty("SFCIOI210Only",false);
    setProperty("SFCIOI210AndFL1100",true);
    device->setProperty("SFCIOUSBHostBusMasterAdmitted",true);
    leaseOwner->setProperty("pcie-usb-BME-error",(UInt32)0,32);
    leaseOwner->setProperty("pcie-usb-BME-ancestor-count",count-1,32);
    return kIOReturnSuccess;
}

bool SFCIOPCIHost::busMasterAllowed(IOPCIAddressSpace space)
{
    // accessLock is held. Never call a mapper/IRQ query here: they read config.
    if(!bmeActive)return false;
    for(UInt32 n=0;n<grantCount;++n){
        auto &g=grants[n];
        if(g.bus!=space.s.busNum || g.slot!=space.s.deviceNum ||
           g.function!=space.s.functionNum)continue;
        auto id=address(space,0,4),code=address(space,8,4);
        return id && code && OSReadLittleInt32((volatile void *)id,0)==g.id &&
            (OSReadLittleInt32((volatile void *)code,0)>>8)==g.code;
    }
    return false;
}

IOReturn SFCIOPCIHost::admitI210(IOPCIDevice *device)
{
    if(!device || ml_at_interrupt_context() || !msi || !interruptPower(this) ||
       device->configRead32(0)!=0x15338086 ||
       (device->configRead32(8)>>8)!=0x020000)return kIOReturnNotReady;
    auto mapper=IOMapper::copyMapperForDevice(device);
    bool ready=mapper && mapper->metaCast("SFCIOPCIDartMapper") &&
        mapper->getProvider()==device &&
        mapper->callPlatformFunction("SFCIOPCIDMAReady",false,device,NULL,NULL,NULL)==kIOReturnSuccess;
    OSSafeReleaseNULL(mapper);
    // The actual interrupt workloop may not have armed the newly enabled
    // parent yet. Caller may retry this factual query during bounded attach.
    if(!ready || !msi->routeReady(device))return kIOReturnNotReady;
    Grant chain[8]={};UInt32 count=0;unsigned depth=0;
    IORegistryEntry *p=device;
    while(p && p!=this && depth++<64){
        auto pci=OSDynamicCast(IOPCIDevice,p);
        if(pci){
            if(count==8)return kIOReturnUnsupported;
            UInt32 id=pci->configRead32(0),code=pci->configRead32(8)>>8;
            if(id==UINT32_MAX || (!count ? pci!=device || code!=0x020000 : code!=0x060400))
                return kIOReturnUnsupported;
            chain[count++]={pci,id,code,pci->getBusNumber(),pci->getDeviceNumber(),pci->getFunctionNumber()};
        }
        p=p->getParentEntry(gIOServicePlane);
    }
    if(p!=this || count<2)return kIOReturnUnsupported;
    // Fresh full inventory: never enable another endpoint, including USB.
    auto it=IORegistryIterator::iterateOver(this,gIOServicePlane,kIORegistryIterateRecursively);
    if(!it)return kIOReturnNoMemory;
    unsigned seen=0,xhci=0,i210=0;bool valid=true;
    while(auto object=it->getNextObject()){
        if(++seen>256){valid=false;break;}
        auto pci=OSDynamicCast(IOPCIDevice,object);if(!pci)continue;
        UInt32 id=pci->configRead32(0),code=pci->configRead32(8)>>8;
        if(id==UINT32_MAX || (pci->configRead16(4)&4)){valid=false;break;}
        if(code==0x0c0330)++xhci;
        else if(id==0x15338086 && code==0x020000 && pci==device)++i210;
        else if(code!=0x060400){valid=false;break;}
    }
    if(!it->isValid())valid=false;it->release();
    if(!valid || xhci!=4 || i210!=1)return kIOReturnNotPermitted;
    // No config lock is held across either real ownership query.
    if(!interruptPower(this) || !msi->routeReady(device))return kIOReturnNotReady;
    IOLockLock(accessLock);
    if(networkAttempted || !view.admitted){IOLockUnlock(accessLock);return kIOReturnNotPermitted;}
    networkAttempted=true;
    for(UInt32 n=0;n<count;++n){grants[n]=chain[n];grants[n].device->retain();}
    grantCount=count;bmeActive=1;
    retain(); // Ambiguous writes/partial DMA remain retained until reboot.
    IOLockUnlock(accessLock);
    // Forwarding bridges first (root to leaf), endpoint last. Every write
    // still passes fresh native physical power/link and identity checks.
    bool ok=true;
    for(UInt32 n=count;n>0;--n){
        auto pci=chain[n-1].device;
        if(!interruptPower(this)){ok=false;break;}
        pci->setBusMasterEnable(true);
        UInt16 command=pci->configRead16(4);
        if(command==UINT16_MAX || !(command&4)){ok=false;break;}
    }
    if(!ok){
        for(UInt32 n=0;n<count;++n){
            auto pci=chain[n].device;UInt16 command=pci->configRead16(4);
            if(command!=UINT16_MAX)pci->setBusMasterEnable(false);
        }
        leaseOwner->setProperty("pcie-i210-BME-error",(UInt32)EIO,32);
        blockConfiguration(); // Gate/pin only; never claim a DMA drain.
        return kIOReturnIOError;
    }
    setProperty("SFCIOPCIDMAAdmitted",true);
    setProperty("SFCIOI210Only",true);
    device->setProperty("SFCIOI210BusMasterAdmitted",true);
    leaseOwner->setProperty("pcie-i210-BME-error",(UInt32)0,32);
    leaseOwner->setProperty("pcie-i210-BME-ancestor-count",count-1,32);
    leaseOwner->setProperty("pcie-endpoint-DMA-admitted",true);
    return kIOReturnSuccess;
}

int SFCIOPCIHost::publishI210Resources(IOPCIDevice *device)
{return publishEndpointResources(device,false);}
int SFCIOPCIHost::publishDockUSBResources(IOPCIDevice *device)
{return publishEndpointResources(device,true);}
int SFCIOPCIHost::resolveDockUSBInterrupt(IOPCIDevice *device)
{
 if(!device || ml_at_interrupt_context() || !msi || !interruptPower(this) ||
    (device->configRead16(4)&4))return ENODEV;
 auto mapper=IOMapper::copyMapperForDevice(device);
 bool ready=mapper && mapper->metaCast("SFCIOPCIDartMapper") &&
  mapper->callPlatformFunction("SFCIOPCIDMAReady",false,device,NULL,NULL,NULL)==kIOReturnSuccess;
 OSSafeReleaseNULL(mapper);if(!ready)return ENODEV;
 int type=0;
 if(device->getInterruptType(0,&type)==kIOReturnSuccess)return EBUSY;
 UInt8 capability=0;
 if(!device->findPCICapability(kIOPCIMSICapability,&capability) || !capability)return ENODEV;
 // Call the public allocator used by IOPCIBridge::resolveMSIInterrupts.
 // This exact retained per-port controller independently checks the actual
 // host, mapper and endpoint identity; no IRQ properties are invented here.
 IOReturn result=msi->allocateDeviceInterrupts(device,0,capability);
 if(result!=kIOReturnSuccess || device->getInterruptType(0,&type)!=kIOReturnSuccess ||
    !(type&kIOInterruptTypePCIMessaged))return EIO;
 device->setProperty("SFCIOUSBMSIAllocated",true);return 0;
}
int SFCIOPCIHost::publishEndpointResources(IOPCIDevice *device,bool usb)
{
 if(!device || ml_at_interrupt_context())return EINVAL;
 static const char *i210Keys[]={"pcie-i210-resource-error","pcie-i210-resource-BARs","pcie-i210-resource-stage","pcie-i210-resource-memory-count"};
 static const char *usbKeys[]={"SFCIOUSBResourceError","SFCIOUSBResourceBARs","SFCIOUSBResourceStage","SFCIOUSBResourceMemoryCount"};
 const char **keys=usb?usbKeys:i210Keys;IORegistryEntry *record=usb?device:leaseOwner;
 auto fail=[record,keys](int error){record->setProperty(keys[0],(UInt32)error,32);return error;};
 if(!interruptPower(this))return fail(ENODEV);
 UInt32 id=device->configRead32(0),code=device->configRead32(8)>>8;
 if(usb ? code!=0x0c0330 || (id!=0x11001b73 && id!=0x12421b21 && id!=0x15d48086) :
          id!=0x15338086 || code!=0x020000)return fail(ENODEV);
 if(device->configRead16(4)&4)return fail(EBUSY);
 IORegistryEntry *parent=device;unsigned depth=0;
 while(parent && parent!=this && depth++<64)parent=parent->getParentEntry(gIOServicePlane);
 if(parent!=this)return fail(ENODEV);
 UInt32 bars[6];for(unsigned n=0;n<6;++n)bars[n]=device->configRead32(0x10+4*n);
 record->setProperty(keys[1],bars,sizeof(bars));
 auto existing=device->getDeviceMemoryWithRegister(0x10);
 if(existing){
  IOByteCount span=0;UInt64 physical=existing->getPhysicalSegment(0,&span,kIOMemoryMapperNone),wanted=0;
  UInt64 bytes=existing->getLength(),bus=0;
  if(sf_pciec_bar_bus(bars,0,&bus) || sf_pciec_bar_cpu(bus,bytes,&wanted) || physical!=wanted || span<bytes)return fail(EIO);
  record->setProperty(keys[2],"existing-BAR0-verified");return fail(0);
 }
 auto property=device->copyProperty("reg");
 struct Release {OSObject *value;~Release(){if(value)value->release();}} releaseProperty={property};
 auto reg=OSDynamicCast(OSData,property);
 if(!reg || !reg->getLength() || reg->getLength()%20 || reg->getLength()>8*20)return fail(EPROTO);
 auto array=OSArray::withCapacity(6);Release releaseArray={array};if(!array)return fail(ENOMEM);
 bool bar0=false;UInt32 seen=0;
 for(unsigned offset=0;offset<reg->getLength();offset+=20){
  UInt32 row[5];memcpy(row,(const UInt8 *)reg->getBytesNoCopy()+offset,20);
  unsigned space=(row[0]>>24)&3,registerNumber=row[0]&0xff;
  if(space!=2 && space!=3)continue; // Config and unsupported I/O-port space are not memory.
  if(registerNumber<0x10 || registerNumber>0x24 || (registerNumber&3))return fail(EPROTO);
  unsigned index=(registerNumber-0x10)/4;
  if(seen&(1u<<index))return fail(EPROTO);seen|=1u<<index;
  UInt64 bytes=((UInt64)row[3]<<32)|row[4],bus=0,cpu=0;
  if(!bytes)return fail(ENXIO);
  int error=sf_pciec_bar_bus(bars,index,&bus);if(error)return fail(error);
  error=sf_pciec_bar_cpu(bus,bytes,&cpu);if(error)return fail(error);
  auto memory=IOMemoryDescriptor::withAddressRange(cpu,bytes,kIODirectionNone|kIOMemoryMapperNone,NULL);
  if(!memory)return fail(ENOMEM);memory->setTag(row[0]);
  bool added=array->setObject(memory);memory->release();if(!added)return fail(ENOMEM);
  if(!index)bar0=true;
 }
 if(!bar0 || !interruptPower(this))return fail(ENODEV);
 // All addresses come from actual live BARs and native enumeration sizes.
 // No BAR, bridge window, BME, or PCI configuration write is performed here.
 device->setDeviceMemory(array);
 if(!device->getDeviceMemoryWithRegister(0x10))return fail(EIO);
 record->setProperty(keys[2],"live-BAR-descriptors-published");
 record->setProperty(keys[3],array->getCount(),32);return fail(0);
}
