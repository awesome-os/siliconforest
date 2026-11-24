// SPDX-License-Identifier: GPL-2.0
// Native IOMapper/IODMACommand adapter, not a second DART implementation.
#include "sf_pci_usb_dma.h"
#include <IOKit/IOMapper.h>
#include <machine/machine_routines.h>
OSDefineMetaClassAndStructors(SFPCIUSBDMABuffer,IOBufferMemoryDescriptor)
static constexpr UInt64 page=0x4000,mask=0x0000000fffffc000ULL,limit=0x10000000;
SFPCIUSBDMABuffer *SFPCIUSBDMABuffer::allocate(IOService *device,UInt64 bytes,UInt64 alignment)
{
 if(!device || ml_at_interrupt_context() || !bytes || bytes>limit ||
    alignment<page || alignment>limit || (alignment&(alignment-1)))return NULL;
 auto property=device->copyProperty("iommu-parent");
 auto actual=OSDynamicCast(IOMapper,property);
 auto resolved=actual?IOMapper::copyMapperForDevice(device):NULL;
 bool ready=actual && resolved==actual && actual->metaCast("SFCIOPCIDartMapper") &&
  actual->callPlatformFunction("SFCIOPCIDMAReady",false,device,NULL,NULL,NULL)==kIOReturnSuccess;
 OSSafeReleaseNULL(resolved);
 if(!ready){OSSafeReleaseNULL(property);return NULL;}
 auto memory=OSTypeAlloc(SFPCIUSBDMABuffer);
 if(!memory){property->release();return NULL;}
 memory->command=NULL;memory->endpoint=NULL;memory->iova=0;memory->retiring=memory->unsafe=0;memory->prepared=false;
 UInt64 span=(bytes+page-1)&~(page-1);
 // PCIe-C is distinct from the coherent internal PCIe fabric. Whole owned
 // buffers use normal-NC DRAM: rings and payloads must not rely on a mapped
 // command's copy synchronization to clean/invalidate the CPU cache.
 if(!memory->initWithPhysicalMask(kernel_task,kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapWriteCombineCache,
                                 span,alignment,mask)){
  property->release();memory->release();return NULL;
 }
 bzero(memory->getBytesNoCopy(),span);
 memory->command=IODMACommand::withSpecification(kIODMACommandOutputHost64,32,0,
                         IODMACommand::kMapped,0,alignment,actual,NULL);
 property->release();
 if(!memory->command){memory->release();return NULL;}
 device->retain();memory->endpoint=device;
 // From here any failed prepare may have installed translations. Keep the
 // original reference as well as any native command/mapper references.
 memory->prepared=true;
 IOReturn error=memory->command->setMemoryDescriptor(memory,true);
 UInt64 offset=0;UInt32 count=1;IODMACommand::Segment64 segment={};
 if(!error)error=memory->command->gen64IOVMSegments(&offset,&segment,&count);
 if(error || count!=1 || !segment.fIOVMAddr || segment.fLength<span ||
    segment.fIOVMAddr>UINT32_MAX || span-1>UINT32_MAX-segment.fIOVMAddr ||
    (segment.fIOVMAddr&(alignment-1))){
  device->setProperty("SFCIOUSBMappingQuarantined",true);
  return NULL;
 }
 device->setProperty("SFCIOUSBDMACPUCachePolicy","requested-normal-noncacheable");
 memory->iova=segment.fIOVMAddr;return memory;
}
IOReturn SFPCIUSBDMABuffer::retire()
{
 if(ml_at_interrupt_context() || __atomic_exchange_n(&retiring,1,__ATOMIC_ACQ_REL))
  return kIOReturnBusy;
 if(!prepared || !command)return kIOReturnNotReady;
 IOReturn error=command->complete();
 if(!error)error=command->clearMemoryDescriptor(false);
 if(error){
  endpoint->setProperty("SFCIOUSBMappingQuarantined",true);
  return error;
 }
 prepared=false;iova=0;OSSafeReleaseNULL(command);OSSafeReleaseNULL(endpoint);
 return kIOReturnSuccess;
}
void SFPCIUSBDMABuffer::free()
{
 if(prepared)return; // No implicit retirement without producer completion.
 OSSafeReleaseNULL(command);OSSafeReleaseNULL(endpoint);
 IOBufferMemoryDescriptor::free();
}
