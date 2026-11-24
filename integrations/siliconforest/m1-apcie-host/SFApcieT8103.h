/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Apple T8103 PCIe host bridge for SiliconForest (J293 port 0, BCM4378).
 * Port handling follows Linux drivers/pci/controller/pcie-apple.c
 * (omacom/linux-aurora):
 *   Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *   Copyright (C) 2021 Google LLC
 *   Copyright (C) 2021 Corellium LLC
 *   Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */
#ifndef _SFAPCIET8103_H
#define _SFAPCIET8103_H

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIBridge.h>
#include <IOKit/pci/IOPCIDevice.h>

/* IOResources-matched loader: promotes /arm-io/apcie to an IOPlatformDevice nub. */
class SFApcieT8103Bridge : public IOService
{
    OSDeclareDefaultStructors(SFApcieT8103Bridge)
public:
    IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

/* The host bridge itself, on the apcie nub. */
class SFApcieT8103Host : public IOPCIBridge
{
    OSDeclareDefaultStructors(SFApcieT8103Host)

    IOMemoryMap *ecamMap;
    IOMemoryMap *portMap;
    volatile UInt8 *ecamBase;
    volatile UInt8 *portBase;
    UInt8 busFirst;
    UInt8 busLast;

    volatile UInt8 *configAddress(IOPCIAddressSpace space, UInt8 offset) const;
    UInt32 portRead(UInt32 offset) const;
    /* MSI: message data n on this port arrives as AIC interrupt msiBase + n */
    UInt32 msiBase;
    UInt32 msiCount;
    UInt32 msiDoorbell;
    UInt32 msiUsed;
    IOPCIDevice *msiOwner[32];
    IOReturn setupMSI(IOService *client, IOPCIDevice *device, UInt32 *vectorOut);
    IOReturn releaseMSI(IOPCIDevice *device);

    /*
     * Port lifecycle after the bring-up in start(). portDown is set by portShutdown() and
     * cleared by portResume(); while it is set no config access reaches the hardware
     * (reads return all ones), because the ECAM and everything behind the link fault then.
     */
    IOLock *portLock;
    OSArray *apcieRegs;      /* what IODTResolveAddressing() returned in start(), retained */
    bool portDown;
    UInt8 secondaryBus;
    bool rootSaved;
    UInt32 rootConfig[8];
    void programRid2sid(void);
    void saveRootPort(void);
    bool restoreRootPort(void);
    IOReturn portShutdown(UInt32 flags, UInt32 *portDownOut);
    IOReturn portResume(UInt32 *linkUpOut);

public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    bool configure(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free(void) APPLE_KEXT_OVERRIDE;
    /*
     * Platform functions for the drivers of devices below this bridge. param1 is always
     * the calling service.
     *   "SFApcieSetupMSI"     (client, IOPCIDevice, UInt32 *aicVector): one MSI vector. A
     *                         device that has one gets the same again, its MSI capability
     *                         is written again (after a power loss).
     *   "SFApcieReleaseMSI"   (client, IOPCIDevice): the vector goes back.
     *   "SFApciePortShutdown" (client, flags, UInt32 *portDown): port and PHY stopped,
     *                         flags bit 0: radio power off through the SMC. The caller must
     *                         be done with its BARs. UNTESTED; SMC "off" value unverified.
     *   "SFApciePortResume"   (client, UInt32 *linkUp): the link is brought up again if it
     *                         is down, root port and stream table restored. UNTESTED.
     * None of the last two is called by anything during a normal boot.
     */
    IOReturn callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
                                  void *param1, void *param2, void *param3, void *param4) APPLE_KEXT_OVERRIDE;
    IODeviceMemory *ioDeviceMemory(void) APPLE_KEXT_OVERRIDE;

    UInt8 firstBusNum(void) APPLE_KEXT_OVERRIDE;
    UInt8 lastBusNum(void) APPLE_KEXT_OVERRIDE;
    IOPCIAddressSpace getBridgeSpace(void) APPLE_KEXT_OVERRIDE;

    UInt32 configRead32(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
    void configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data) APPLE_KEXT_OVERRIDE;
    UInt16 configRead16(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
    void configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data) APPLE_KEXT_OVERRIDE;
    UInt8 configRead8(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
    void configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data) APPLE_KEXT_OVERRIDE;
};

#endif
