// SPDX-License-Identifier: GPL-2.0
/*
 * Apple T8103 PCIe host bridge for SiliconForest (J293 port 0, BCM4378).
 *
 * Register names and the port model follow Linux
 * drivers/pci/controller/pcie-apple.c (omacom/linux-aurora):
 *   Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *   Copyright (C) 2021 Google LLC
 *   Copyright (C) 2021 Corellium LLC
 *   Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 *   Author: Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *   Author: Marc Zyngier <maz@kernel.org>
 * The BAR0 window constants follow Linux brcmfmac/pcie.c (ISC, Broadcom).
 *
 * Link bring-up is in SFApcieT8103Link.cpp: when port 0 is not already up, the
 * bridge powers and initialises the controller, PHY and port itself (ported from
 * m1n1) and runs the Linux port link sequence. A link that is already up is
 * used as it is. The link sequence also powers the radio through the SMC (key
 * gP0d, SFAppleSMC.cpp); boot argument sfapciesmc=0 turns that off for a
 * launcher that powers the radio itself.
 *
 * The way down is portShutdown(): from stop(), and for the driver of the device
 * behind the port through the platform function SFApciePortShutdown. Nothing runs
 * it during a normal boot. portResume() (SFApciePortResume) is the way back.
 */
#include "SFApcieT8103.h"
#include "SFApcieT8103Link.h"

#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOInterruptController.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/OSAtomic.h>
#include <libkern/OSByteOrder.h>
#include <pexpert/pexpert.h>

#define LOG(fmt, ...) IOLog("SFApcieT8103: " fmt "\n", ##__VA_ARGS__)

/* pcie-apple.c */
#define PORT_INTSTAT        0x00100
#define PORT_LINKSTS        0x00208
#define   PORT_LINKSTS_UP   (1u << 0)
#define PORT_APPCLK         0x00800
#define PORT_STATUS         0x00804
#define   PORT_STATUS_READY (1u << 0)
#define PORT_REFCLK         0x00810
#define PORT_PERST          0x00814
#define PORT_RID2SID        0x00828
#define   PORT_RID2SID_VALID     (1u << 31)
#define   PORT_RID2SID_SID_SHIFT 16
#define   PORT_RID2SID_BUS_SHIFT 8

/* ADT reg indices of /arm-io/apcie (m1n1 src/pcie.c) */
#define APCIE_REG_ECAM      0
#define APCIE_REG_PORT0     6

#define ECAM_BUS_SHIFT      20
#define ECAM_DEVICE_SHIFT   15
#define ECAM_FUNCTION_SHIFT 12

/* 32-bit memory window: PCI 0xc0000000 -> CPU 0x6c0000000 through the ADT ranges */
#define APCIE_MEM32_BUS_BASE 0xc0000000ULL
#define APCIE_MEM32_SIZE     0x40000000ULL

/* ---- loader -------------------------------------------------------------- */

#undef super
#define super IOService
OSDefineMetaClassAndStructors(SFApcieT8103Bridge, IOService)

static volatile UInt32 gLoaderClaimed = 0;

IOService *SFApcieT8103Bridge::probe(IOService *provider, SInt32 *score)
{
    int enabled = 0;
    if (!PE_parse_boot_argn("sfapciebridge", &enabled, sizeof(enabled)) || enabled != 1)
        return NULL;
    if (!OSCompareAndSwap(0, 1, &gLoaderClaimed))
        return NULL;
    return super::probe(provider, score);
}

bool SFApcieT8103Bridge::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    IORegistryEntry *entry = IORegistryEntry::fromPath("/arm-io/apcie", gIODTPlane);
    if (!entry) {
        LOG("no /arm-io/apcie in the device tree");
        return false;
    }

    IOService *nub = OSDynamicCast(IOService, entry);
    if (nub) {
        const OSMetaClass *meta = nub->getMetaClass();
        IOService *parent = nub->getProvider();
        LOG("/arm-io/apcie is already a service: class %s name %s provider %s registered=%d",
            meta ? meta->getClassName() : "?", nub->getName(), parent ? parent->getName() : "(none)",
            (int)((nub->getState() & kIOServiceRegisteredState) != 0));
    } else {
        IOPlatformDevice *made = OSTypeAlloc(IOPlatformDevice);
        bool ok = made && made->init(entry, gIODTPlane);
        entry->release();
        entry = NULL;
        if (ok)
            ok = made->attach(getPlatform());
        if (!ok) {
            LOG("could not publish the apcie nub");
            OSSafeReleaseNULL(made);
            return false;
        }
        LOG("published apcie nub %s", made->getName());
        nub = made;
    }

    /* Device tree services on this kernel are not in the service plane; put ours there. */
    if (!nub->inPlane(gIOServicePlane)) {
        bool attached = nub->attach(getPlatform());
        LOG("attached apcie to the platform expert in the service plane: %s", attached ? "ok" : "FAILED");
    }

    /* Attach and start the host bridge directly: no dependence on how the nub matches. */
    SFApcieT8103Host *host = OSTypeAlloc(SFApcieT8103Host);
    bool ok = host && host->init();
    if (ok)
        ok = host->attach(nub);
    if (ok) {
        ok = host->start(nub);
        if (!ok)
            host->detach(nub);
    }
    LOG("host bridge %s", ok ? "started" : "did not start");
    OSSafeReleaseNULL(host);
    nub->release();
    return true;
}

/* ---- host bridge --------------------------------------------------------- */

#undef super
#define super IOPCIBridge
OSDefineMetaClassAndStructors(SFApcieT8103Host, IOPCIBridge)

UInt32 SFApcieT8103Host::portRead(UInt32 offset) const
{
    UInt32 v = OSReadLittleInt32((volatile void *)portBase, offset);
    OSSynchronizeIO();
    return v;
}

bool SFApcieT8103Host::start(IOService *provider)
{
    /* Resolve "reg" through the device tree directly; the nub may not have resources yet. */
    OSArray *regs = IODTResolveAddressing(provider, "reg", NULL);
    IOMemoryDescriptor *ecam = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(APCIE_REG_ECAM)) : NULL;
    IOMemoryDescriptor *port = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(APCIE_REG_PORT0)) : NULL;
    LOG("apcie reg entries: %u", regs ? regs->getCount() : 0);
    UInt32 range[2] = { 0, 0 };
    OSData *busRange = OSDynamicCast(OSData, provider->getProperty("bus-range"));

    if (!ecam || !port) {
        LOG("apcie nub has no ECAM/port0 reg (ecam=%p port=%p)", ecam, port);
        return false;
    }
    LOG("ecam phys=0x%llx len=0x%llx port0 phys=0x%llx len=0x%llx",
        (unsigned long long)ecam->getPhysicalSegment(0, NULL, kIOMemoryMapperNone), (unsigned long long)ecam->getLength(),
        (unsigned long long)port->getPhysicalSegment(0, NULL, kIOMemoryMapperNone), (unsigned long long)port->getLength());

    /* MSI facts from the apcie node; port 0's share starts at message 0 */
    msiBase = 704;
    msiCount = 8;
    msiDoorbell = 0xfffff000;
    msiUsed = 0;
    {
        OSData *d;
        IORegistryEntry *bridge0 = IORegistryEntry::fromPath("/arm-io/apcie/pci-bridge0", gIODTPlane);
        if ((d = OSDynamicCast(OSData, provider->getProperty("msi-vector-offset"))) && d->getLength() >= 4)
            msiBase = *(const UInt32 *)d->getBytesNoCopy();
        if ((d = OSDynamicCast(OSData, provider->getProperty("msi-address"))) && d->getLength() >= 4)
            msiDoorbell = *(const UInt32 *)d->getBytesNoCopy();
        if (bridge0 && (d = OSDynamicCast(OSData, bridge0->getProperty("#msi-vectors"))) && d->getLength() >= 4)
            msiCount = *(const UInt32 *)d->getBytesNoCopy();
        if (bridge0 && (d = OSDynamicCast(OSData, bridge0->getProperty("msi-vector-base"))) && d->getLength() >= 4)
            msiBase += *(const UInt32 *)d->getBytesNoCopy();
        OSSafeReleaseNULL(bridge0);
        if (msiCount > 32)
            msiCount = 32;
    }

    portLock = IOLockAlloc();
    if (!portLock)
        return false;
    /* Kept for portShutdown()/portResume(): the apcie "reg" is never resolved a second time. */
    apcieRegs = regs;
    apcieRegs->retain();

    busFirst = 0;
    busLast = 8;
    if (busRange && busRange->getLength() >= sizeof(range)) {
        memcpy(range, busRange->getBytesNoCopy(), sizeof(range));
        if (range[0] <= range[1] && range[1] <= 255) {
            busFirst = (UInt8)range[0];
            busLast = (UInt8)range[1];
        }
    }
    UInt64 buses = ecam->getLength() >> ECAM_BUS_SHIFT;
    if (buses == 0)
        return false;
    if ((UInt64)busLast - busFirst + 1 > buses)
        busLast = (UInt8)(busFirst + buses - 1);

    portMap = port->map(kIOMapInhibitCache);
    if (!portMap) {
        LOG("could not map port0");
        return false;
    }
    portBase = (volatile UInt8 *)portMap->getVirtualAddress();

    /*
     * Bring the link up unless it already is. This comes before any port register read
     * here: with the apcie power domains off the port block cannot be read.
     * sfapcielink=0: no bring-up here, something before the kernel must have done it all.
     */
    int linkMode = 1;
    PE_parse_boot_argn("sfapcielink", &linkMode, sizeof(linkMode));
    if (linkMode != 0) {
        UInt32 broughtUp = 0;
        IOReturn linkResult = SFApcieT8103LinkBringUp(provider, regs, &broughtUp);
        if (linkResult != kIOReturnSuccess) {
            LOG("port0 link bring-up failed (0x%08x, linksts 0x%08x). Not enumerating.", linkResult, broughtUp);
            return false;
        }
    }

    UInt32 status = portRead(PORT_STATUS), link = portRead(PORT_LINKSTS);
    LOG("port0 appclk=0x%08x status=0x%08x refclk=0x%08x perst=0x%08x linksts=0x%08x intstat=0x%08x",
        portRead(PORT_APPCLK), status, portRead(PORT_REFCLK), portRead(PORT_PERST), link,
        portRead(PORT_INTSTAT));
    if (!(status & PORT_STATUS_READY) || !(link & PORT_LINKSTS_UP)) {
        LOG("port0 link is not up. Not enumerating.");
        return false;
    }

    /* Map only the buses we may touch. */
    IOByteCount ecamLen = ((IOByteCount)(busLast - busFirst) + 1) << ECAM_BUS_SHIFT;
    ecamMap = ecam->map(kIOMapInhibitCache);
    if (!ecamMap || ecamMap->getLength() < ecamLen) {
        LOG("could not map the ECAM window");
        return false;
    }
    ecamBase = (volatile UInt8 *)ecamMap->getVirtualAddress();

    IOPCIAddressSpace root;
    root.bits = 0;
    LOG("root port id=0x%08x class=0x%08x buses=0x%08x; bus range %u-%u",
        configRead32(root, kIOPCIConfigVendorID), configRead32(root, kIOPCIConfigRevisionID),
        configRead32(root, 0x18), busFirst, busLast);

    /* regs is owned by the registry (IODTResolveAddressing publishes it); not released here. */
    bool ok = super::start(provider);
    LOG("IOPCIBridge start %s", ok ? "ok" : "FAILED");
    if (ok) {
        IOPCIAddressSpace dev;
        dev.bits = 0;
        dev.s.busNum = (configRead32(root, 0x18) >> 8) & 0xff;
        for (unsigned fn = 0; fn < 2 && dev.s.busNum; fn++) {
            dev.s.functionNum = fn;
            LOG("after scan %u:0.%u id=0x%08x cmd=0x%08x bar0=0x%08x%08x bar2=0x%08x%08x",
                dev.s.busNum, fn, configRead32(dev, kIOPCIConfigVendorID),
                configRead32(dev, kIOPCIConfigCommand),
                configRead32(dev, 0x14), configRead32(dev, 0x10),
                configRead32(dev, 0x1c), configRead32(dev, 0x18));
        }
        secondaryBus = dev.s.busNum;
        programRid2sid();
        saveRootPort();
        LOG("root port after scan buses=0x%08x membase/limit=0x%08x",
            configRead32(root, 0x18), configRead32(root, 0x20));
    }
    return ok;
}

/*
 * apple_pcie_enable_device(): requester ID -> DART stream. Linux iommu-map sends every
 * function on bus 1 to stream 1 of dart-apcie0. The link sequence clears the table, so
 * this runs after every bring-up.
 */
void SFApcieT8103Host::programRid2sid(void)
{
    for (unsigned fn = 0; fn < 2 && secondaryBus; fn++) {
        UInt32 entry = PORT_RID2SID_VALID | (1u << PORT_RID2SID_SID_SHIFT) |
                       ((UInt32)secondaryBus << PORT_RID2SID_BUS_SHIFT) | fn;
        OSWriteLittleInt32((volatile void *)portBase, PORT_RID2SID + 4 * fn, entry);
        OSSynchronizeIO();
        LOG("rid2sid[%u] = 0x%08x (readback 0x%08x)", fn, entry, portRead(PORT_RID2SID + 4 * fn));
    }
}

/* What IOPCIFamily wrote to the root port and a controller re-initialisation clears. */
static const UInt8 kRootConfig[8] = { 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x3c, 0x04 };

void SFApcieT8103Host::saveRootPort(void)
{
    IOPCIAddressSpace root;
    root.bits = 0;
    for (unsigned i = 0; i < 8; i++)
        rootConfig[i] = configRead32(root, kRootConfig[i]);
    rootSaved = true;
}

bool SFApcieT8103Host::restoreRootPort(void)
{
    IOPCIAddressSpace root;
    bool lost = false;
    root.bits = 0;
    if (!rootSaved)
        return false;
    for (unsigned i = 0; i < 8; i++) {
        UInt32 now = configRead32(root, kRootConfig[i]);
        /* 0x04: the status half is write-one-to-clear, only the command half goes back */
        if (kRootConfig[i] == 0x04) {
            if ((now & 0xffff) != (rootConfig[i] & 0xffff)) {
                configWrite16(root, 0x04, (UInt16)rootConfig[i]);
                lost = true;
            }
        } else if (now != rootConfig[i]) {
            configWrite32(root, kRootConfig[i], rootConfig[i]);
            lost = true;
        }
    }
    return lost;
}

/*
 * Port 0 down: see SFApcieT8103PortShutdown() for the register order. From here on the
 * ECAM is not touched (configAddress() returns nothing while portDown is set): IOPCIFamily
 * may still read config space of the devices it knows, and gets all ones.
 * UNTESTED on hardware; the SMC radio "off" value is unverified.
 */
IOReturn SFApcieT8103Host::portShutdown(UInt32 flags, UInt32 *portDownOut)
{
    IOReturn ret = kIOReturnSuccess;

    IOLockLock(portLock);
    if (portDown) {
        LOG("shutdown: port0 is already down");
    } else if (!apcieRegs || !portBase) {
        LOG("shutdown: the bridge never started, nothing to shut down");
        ret = kIOReturnNotReady;
    } else {
        bool down = false;
        LOG("shutdown: port0 going down%s (UNTESTED sequence)",
            (flags & kSFApcieShutdownRadioOff) ? ", then radio power off through the SMC" : "");
        /* nothing reaches for the ECAM once the sequence has started */
        portDown = true;
        ret = SFApcieT8103PortShutdown(getProvider(), apcieRegs, flags, &down);
        if (!down) {
            portDown = false;
            LOG("shutdown: port0 was NOT shut down (0x%08x)", ret);
        } else {
            LOG("shutdown: port0 is down; config space reads all ones until SFApciePortResume");
        }
    }
    if (portDownOut)
        *portDownOut = portDown ? 1 : 0;
    IOLockUnlock(portLock);
    return ret;
}

/*
 * Port 0 up again, after portShutdown() or after the SoC took the controller's power
 * (system sleep): the bring-up of start() decides by itself what is needed, from nothing
 * (link already up) to the whole controller, PHY and port sequence with the radio power
 * write. Then the root port's config registers and the RID-to-stream table, which a
 * bring-up clears, are written again. The devices behind the port come back with empty
 * config space: restoring that is their drivers' business. UNTESTED on hardware.
 */
IOReturn SFApcieT8103Host::portResume(UInt32 *linkUpOut)
{
    IOReturn ret;
    UInt32 linksts = 0;

    if (linkUpOut)
        *linkUpOut = 0;
    IOLockLock(portLock);
    if (!apcieRegs || !portBase || !ecamBase) {
        IOLockUnlock(portLock);
        LOG("resume: the bridge never started");
        return kIOReturnNotReady;
    }
    LOG("resume: port0 %s, running the link bring-up (UNTESTED path)",
        portDown ? "was shut down" : "is expected up");
    ret = SFApcieT8103LinkBringUp(getProvider(), apcieRegs, &linksts);
    if (ret != kIOReturnSuccess) {
        /* in whatever state the port is, nothing may reach behind it */
        portDown = true;
        LOG("resume: port0 link did not come up (0x%08x, linksts 0x%08x); config space stays closed", ret, linksts);
    } else {
        portDown = false;
        bool lost = restoreRootPort();
        programRid2sid();
        LOG("resume: port0 link up (linksts 0x%08x), root port config %s", linksts,
            lost ? "was lost and is restored" : "intact");
        if (linkUpOut)
            *linkUpOut = 1;
    }
    IOLockUnlock(portLock);
    return ret;
}

/*
 * The bridge is going away: IOPCIFamily has stopped the devices behind it. Port and PHY
 * are stopped and the radio's power is switched off (not with sfapciesmc=0). This never
 * runs during a normal boot; a start() that fails does not come here either.
 */
void SFApcieT8103Host::stop(IOService *provider)
{
    LOG("stop: shutting port0 down");
    if (portLock)
        portShutdown(kSFApcieShutdownRadioOff, NULL);
    super::stop(provider);
}

/*
 * One MSI vector for a function below this bridge, as apple_msi_domain_alloc() /
 * apple_msi_compose_msg() do in Linux pcie-apple.c: the device is told to write
 * message data n to the port's doorbell, and the port raises AIC interrupt
 * msi-vector-offset + n. The interrupt is described on the client service (controller
 * and specifier properties), so the client attaches an ordinary IOInterruptEventSource
 * to itself, index 0.
 */
IOReturn SFApcieT8103Host::setupMSI(IOService *client, IOPCIDevice *device, UInt32 *vectorOut)
{
    UInt8 cap = 0;
    UInt32 n;

    if (!client || !device)
        return kIOReturnBadArgument;
    if (portDown)
        return kIOReturnNotReady;
    if (device->findPCICapability(kIOPCIMSICapability, &cap) == 0 || cap == 0)
        return kIOReturnUnsupported;
    /* a device that has a vector keeps it: this is then the rewrite after a power loss */
    for (n = 0; n < msiCount && msiOwner[n] != device; n++) {}
    if (n >= msiCount)
        for (n = 0; n < msiCount && (msiUsed & (1u << n)); n++) {}
    if (n >= msiCount)
        return kIOReturnNoResources;

    IOInterruptVectorNumber vector = msiBase + n;
    const OSSymbol *controller = gIODTDefaultInterruptController;
    OSData *spec = OSData::withBytes(&vector, sizeof(vector));
    OSArray *controllers = OSArray::withObjects((const OSObject **)&controller, 1);
    OSArray *specs = spec ? OSArray::withObjects((const OSObject **)&spec, 1) : NULL;
    bool described = controllers && specs &&
        client->setProperty(gIOInterruptControllersKey, controllers) &&
        client->setProperty(gIOInterruptSpecifiersKey, specs);
    OSSafeReleaseNULL(spec);
    OSSafeReleaseNULL(controllers);
    OSSafeReleaseNULL(specs);
    if (!described)
        return kIOReturnNoMemory;

    UInt16 ctl = device->configRead16(cap + 2);
    device->configWrite32(cap + 4, msiDoorbell);
    if (ctl & 0x80) {                       /* 64-bit capable */
        device->configWrite32(cap + 8, 0);
        device->configWrite16(cap + 12, (UInt16)n);
    } else {
        device->configWrite16(cap + 8, (UInt16)n);
    }
    device->configWrite16(cap + 2, (ctl & ~0x70) | 0x1);    /* one message, enable */
    device->configWrite16(kIOPCIConfigCommand, device->configRead16(kIOPCIConfigCommand) | 0x400); /* INTx off */
    if (!(device->configRead16(cap + 2) & 1))
        return kIOReturnIOError;
    msiUsed |= 1u << n;
    msiOwner[n] = device;
    if (vectorOut)
        *vectorOut = (UInt32)vector;
    LOG("msi: %u:%u.%u message %u -> AIC interrupt %u (doorbell 0x%08x) for %s",
        device->getBusNumber(), device->getDeviceNumber(), device->getFunctionNumber(), n,
        (UInt32)vector, msiDoorbell, client->getName());
    return kIOReturnSuccess;
}

/* The device stops sending MSIs (unless the port is down, then it cannot) and its vector is free. */
IOReturn SFApcieT8103Host::releaseMSI(IOPCIDevice *device)
{
    UInt8 cap = 0;
    UInt32 n;

    if (!device)
        return kIOReturnBadArgument;
    for (n = 0; n < msiCount && msiOwner[n] != device; n++) {}
    if (n >= msiCount)
        return kIOReturnNotFound;
    if (!portDown && device->findPCICapability(kIOPCIMSICapability, &cap) != 0 && cap != 0)
        device->configWrite16(cap + 2, device->configRead16(cap + 2) & ~0x1);
    msiOwner[n] = NULL;
    msiUsed &= ~(1u << n);
    LOG("msi: message %u released%s", n, portDown ? " (port down, device not written)" : "");
    return kIOReturnSuccess;
}

IOReturn SFApcieT8103Host::callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
                                                void *param1, void *param2, void *param3, void *param4)
{
    if (functionName && functionName->isEqualTo("SFApcieSetupMSI"))
        return setupMSI((IOService *)param1, OSDynamicCast(IOPCIDevice, (OSObject *)param2), (UInt32 *)param3);
    if (functionName && functionName->isEqualTo("SFApcieReleaseMSI"))
        return releaseMSI(OSDynamicCast(IOPCIDevice, (OSObject *)param2));
    if (functionName && functionName->isEqualTo("SFApciePortShutdown"))
        return portShutdown((UInt32)(uintptr_t)param2, (UInt32 *)param3);
    if (functionName && functionName->isEqualTo("SFApciePortResume"))
        return portResume((UInt32 *)param2);
    return super::callPlatformFunction(functionName, waitForFunction, param1, param2, param3, param4);
}

bool SFApcieT8103Host::configure(IOService *provider)
{
    bool ok = addBridgeMemoryRange((IOPhysicalAddress)APCIE_MEM32_BUS_BASE,
                                   (IOPhysicalLength)APCIE_MEM32_SIZE, true);
    LOG("memory window pci 0x%llx size 0x%llx %s", APCIE_MEM32_BUS_BASE, APCIE_MEM32_SIZE,
        ok ? "added" : "NOT added");
    return super::configure(provider);
}

void SFApcieT8103Host::free(void)
{
    OSSafeReleaseNULL(apcieRegs);
    if (portLock) {
        IOLockFree(portLock);
        portLock = NULL;
    }
    OSSafeReleaseNULL(ecamMap);
    OSSafeReleaseNULL(portMap);
    ecamBase = NULL;
    portBase = NULL;
    super::free();
}

/* No I/O port space behind this bridge. */
IODeviceMemory *SFApcieT8103Host::ioDeviceMemory(void) { return NULL; }

UInt8 SFApcieT8103Host::firstBusNum(void) { return busFirst; }
UInt8 SFApcieT8103Host::lastBusNum(void) { return busLast; }

IOPCIAddressSpace SFApcieT8103Host::getBridgeSpace(void)
{
    IOPCIAddressSpace space;
    space.bits = 0;
    space.s.busNum = busFirst;
    return space;
}

volatile UInt8 *SFApcieT8103Host::configAddress(IOPCIAddressSpace space, UInt8 offset) const
{
    if (!ecamBase || space.s.busNum < busFirst || space.s.busNum > busLast)
        return NULL;
    /* port shut down: the ECAM, root port included, is not touched (see portShutdown()) */
    if (portDown)
        return NULL;
    /*
     * Root bus: only port 0 (device 0, function 0) is initialised on the J293.
     * Below it: a PCIe link has exactly one device, number 0.
     */
    if (space.s.deviceNum != 0)
        return NULL;
    if (space.s.busNum == busFirst && space.s.functionNum != 0)
        return NULL;

    IOByteCount address = ((IOByteCount)(space.s.busNum - busFirst) << ECAM_BUS_SHIFT) |
                          ((IOByteCount)space.s.deviceNum << ECAM_DEVICE_SHIFT) |
                          ((IOByteCount)space.s.functionNum << ECAM_FUNCTION_SHIFT) |
                          ((IOByteCount)space.es.registerNumExtended << 8) | offset;
    return ecamBase + address;
}

UInt32 SFApcieT8103Host::configRead32(IOPCIAddressSpace space, UInt8 offset)
{
    volatile UInt8 *address = configAddress(space, offset & ~3);
    if (!address)
        return 0xffffffff;
    UInt32 value = OSReadLittleInt32((volatile void *)address, 0);
    OSSynchronizeIO();
    return value;
}

void SFApcieT8103Host::configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data)
{
    volatile UInt8 *address = configAddress(space, offset & ~3);
    if (!address)
        return;
    OSWriteLittleInt32((volatile void *)address, 0, data);
    OSSynchronizeIO();
}

UInt16 SFApcieT8103Host::configRead16(IOPCIAddressSpace space, UInt8 offset)
{
    volatile UInt8 *address = configAddress(space, offset & ~1);
    if (!address)
        return 0xffff;
    UInt16 value = OSReadLittleInt16((volatile void *)address, 0);
    OSSynchronizeIO();
    return value;
}

void SFApcieT8103Host::configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data)
{
    volatile UInt8 *address = configAddress(space, offset & ~1);
    if (!address)
        return;
    OSWriteLittleInt16((volatile void *)address, 0, data);
    OSSynchronizeIO();
}

UInt8 SFApcieT8103Host::configRead8(IOPCIAddressSpace space, UInt8 offset)
{
    volatile UInt8 *address = configAddress(space, offset);
    if (!address)
        return 0xff;
    UInt8 value = *address;
    OSSynchronizeIO();
    return value;
}

void SFApcieT8103Host::configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data)
{
    volatile UInt8 *address = configAddress(space, offset);
    if (!address)
        return;
    *address = data;
    OSSynchronizeIO();
}

