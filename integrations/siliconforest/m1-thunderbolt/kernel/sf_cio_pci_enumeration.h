/* SPDX-License-Identifier: MIT */
#ifndef SF_CIO_PCI_ENUMERATION_H
#define SF_CIO_PCI_ENUMERATION_H
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include "sf_cio_pcie_controller.h"
#include "sf_cio_pcie_host.h"
#ifdef SF_CIO_ENDPOINT_MAPPER
#include "sf_cio_pci_mapper.h"
#endif
/* Private, enumeration-only profile. No matching personality or DMA/MSI
 * admission. prepare/activated/deactivate are actual Linux CM hooks.
 * publishAfterApproval is called ONLY after real TS3 approval returns,
 * outside the domain lock and after the post-activation callback returned.
 * A failed/removed programmed host retains resources until reboot. This is
 * not hot-remove support and cannot satisfy the ten-cycle milestone.
 */
class SFCIOPCIEnumeration : public OSObject {
 OSDeclareDefaultStructors(SFCIOPCIEnumeration)
 IOService *owner,*nub;
 void *context;
 bool (*powerProof)(void *);
 IOLock *lock;
 SFCIOPCIController *controller;
 SFCIOPCIHost *host;
 uint32_t posted,committed,attempted,revoked;
 bool preparing;
#ifdef SF_CIO_ENDPOINT_MAPPER
 SFCIOPCIDartMapper *i210Mapper;
 bool mapperAttempted;
#if SF_CIO_I210_NETWORK
 IOPCIDevice *usbMapperEndpoint;
 bool usbMapperAttempted;
#endif
#endif
 static bool power(void *);
 static bool tunnel(void *);
 int catalogueSafe();
 int fail(int);
public:
 bool initWithOwner(IOService *,void *,bool (*)(void *));
 int checkProfile(); // real catalogue metadata only, no PCIe MMIO
 int prepare();
 int activated();
 int deactivate();
 int publishAfterApproval();
#ifdef SF_CIO_ENDPOINT_MAPPER
 int prepareI210Mapper(); // IOMapper; explicit NETWORK variant then retries I210 matching.
#if SF_CIO_I210_NETWORK
 int prepareDockUSBMapper(); // Source-only first-FL1100 factory; no driver/BME grant.
#endif
#endif
 bool blocksStop() const;
 bool pristineForRetrain(); // No reserved controller or attempted host.
 void free() override;
};
#endif
