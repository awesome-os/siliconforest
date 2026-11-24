/* SPDX-License-Identifier: GPL-2.0 */
/* Native T8103 PCIe-C MSI transport. Uses the actual frozen IOPCIFamily
 * controller ABI and AIC IOService interrupt API, not fabricated indexes.
 * Per-port MSI mapping from pinned pcie-apple.c (Asahi Linux Contributors).
 * Source/SDK variant only; no BME admission or hot-remove/drain claim.
 */
#ifndef SF_CIO_PCI_MSI_H
#define SF_CIO_PCI_MSI_H
#include <IOKit/pci/IOPCIPrivate.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOWorkLoop.h>
class SFCIOPCIMSIController : public IOPCIMessagedInterruptController {
 OSDeclareDefaultStructors(SFCIOPCIMSIController)
 struct Parent {
  SFCIOPCIMSIController *owner;
  IOService *nub;
  IOInterruptEventSource *soft;
  UInt32 slot,enabled,armed,delivered,fired;
  bool registered;
 } parents[32];
 IOService *powerOwner;
 IOWorkLoop *loop;
 void *powerContext;
 bool (*powerProof)(void *);
 UInt32 base,count,doorbell,accepting,failed;
 bool pinned;
 static void primaryInterrupt(OSObject *,void *,IOService *,int);
 static void rearm(OSObject *,IOInterruptEventSource *,int);
 bool deviceAllowed(IOPCIDevice *);
 bool mapperReady(IOPCIDevice *);
 void fault();
public:
 bool initForPort(IOService *,void *,bool (*)(void *),UInt32,UInt32,UInt32);
 bool ready();
 void revoke(); // Nonblocking gate only, never a child/IRQ/work drain ACK.
 IOReturn messageForDevice(IOService *,UInt32,UInt32 *);
 bool routeReady(IOPCIDevice *); // Explicit thread-context factual query.
 uint32_t getDeviceMSILimit(IOPCIDevice *,uint32_t) override;
 IOReturn registerInterrupt(IOService *,int,void *,IOInterruptHandler,void *) override;
 void enableVector(IOInterruptVectorNumber,IOInterruptVector *) override;
 void disableVectorHard(IOInterruptVectorNumber,IOInterruptVector *) override;
 void free() override;
};
#endif
