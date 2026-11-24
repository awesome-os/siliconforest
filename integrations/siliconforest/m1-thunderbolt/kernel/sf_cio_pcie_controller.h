/* SPDX-License-Identifier: GPL-2.0 */
/* Native adapter for sf_pciec_port, following pinned pcie-apple.c.
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2021 Google LLC
 * Copyright (C) 2021 Corellium LLC
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */
#ifndef SF_CIO_PCIE_CONTROLLER_H
#define SF_CIO_PCIE_CONTROLLER_H
#include "sf_soc.h"
#include "sf_pciec_port.h"
#include "sf_pciec_requesters.h"
#include "sf_pciec_dart_block.h"
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
/* Unwired native controller backend: no matching personality, endpoint
 * publication, MSI or DMA translation. Fixed-layout DART blocking only;
 * never reads PCIe-C parameters. The future CM adapter must own the actual
 * ACIO/PHY lease and supply real power and activated-tunnel proofs. Never run
 * coldInit or linkStart while holding the CM domain lock: its config traffic
 * must run. BOTH require a real activated-tunnel proof, after the CM's
 * post-activation callback has returned, matching Linux child population.
 * Any attempted hardware programming pins ownership/mappings until reboot;
 * revoke blocks new accesses but supplies NO hardware or child drain proof.
 */
class SFCIOPCIController : public OSObject {
 OSDeclareDefaultStructors(SFCIOPCIController)
 friend class SFCIOPCIDartMapper;
 IOService *owner;
 void *context;
 bool (*powerProof)(void *),(*tunnelProof)(void *);
 IOLock *lock;
 SFMmio windows[5],powerGate,endpointDart;
 bool admitted,touched,poisoned,cold,linked;
 sf_pciec_io io;
 sf_pciec_requesters requesters;
 OSObject *dmaOwner; // One native SID0 owner; never an endpoint admission flag.
 bool live();
 static int read(void *,sf_pciec_window,uint32_t,uint32_t *);
 static int write(void *,sf_pciec_window,uint32_t,uint32_t);
 static int tune(void *,sf_pciec_window);
 static void delay(void *,unsigned);
 bool endpointAccess();
 static int dartRead(void *,uint32_t,uint32_t *);
 static int dartWrite(void *,uint32_t,uint32_t);
public:
 bool initWithOwner(IOService *,void *,bool (*)(void *),bool (*)(void *));
 int coldInit();
 int linkStart();
 // Mapping-table reservation only; no BME/DART/MSI or endpoint admission.
 int reserveRequester(unsigned bus,unsigned device,unsigned function,uint32_t *sid);
 // After real link-up, before publishing an enumeration-only PCI bridge.
 // Blocks all64 streams; supplies no translation or endpoint drain proof.
 int blockEndpointDMA();
 int revoke();
 void free() override;
};
#endif
