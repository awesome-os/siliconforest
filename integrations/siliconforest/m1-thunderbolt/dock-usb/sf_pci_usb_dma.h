/* SPDX-License-Identifier: GPL-2.0 */
/* Native PCI xHCI glue; never use the direct USB1 DART for a dock device. */
#ifndef SF_PCI_USB_DMA_H
#define SF_PCI_USB_DMA_H
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOService.h>
class SFPCIUSBDMABuffer : public IOBufferMemoryDescriptor {
 OSDeclareDefaultStructors(SFPCIUSBDMABuffer)
 IODMACommand *command;
 IOService *endpoint;
 UInt64 iova;
 UInt32 retiring;
 UInt32 unsafe;
 bool prepared;
 void free() override;
public:
 static SFPCIUSBDMABuffer *allocate(IOService *,UInt64 bytes,UInt64 alignment);
 UInt64 dmaAddress() const {return iova;}
 void markUnsafeForReuse() {__atomic_store_n(&unsafe,1,__ATOMIC_RELEASE);}
 bool unsafeForReuse() const {return __atomic_load_n(&unsafe,__ATOMIC_ACQUIRE)!=0;}
 // The producer must already have completed/halted. Caller retains its own
 // reference across this call: the real command and mapper also retain us.
 // Failure retains command/backing until reboot; never call release after it.
 IOReturn retire();
};
#endif
