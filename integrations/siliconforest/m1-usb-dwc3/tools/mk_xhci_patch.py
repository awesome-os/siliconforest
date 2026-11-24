#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the reviewed SDK patch; fail if an expected upstream site changes."""
import argparse
import difflib
from pathlib import Path


def replace(s, old, new, count=1):
    assert s.count(old) == count, (old[:100], s.count(old), count)
    return s.replace(old, new)


def transform(name, s):
    if name == 'RavynXHCIUSBBus.h':
        return replace(s, '    bool initWithPort(RavynXHCIPort *port);',
                       '    bool initWithPort(RavynXHCIPort *port);\n    void free() override;')
    if name == 'RavynXHCIUSBBus.cpp':
        s = replace(s, '    fPort = port;', '    fPort = port;\n    fPort->retain();')
        return s + '\nvoid RavynXHCIUSBBus::free()\n{\n    OSSafeReleaseNULL(fPort);\n    IOUSBController::free();\n}\n'
    if name == 'RavynXHCIMassStorageDisk.cpp':
        s = replace(s, '    fParent = parent;', '    fParent = parent;\n    fParent->retain();')
        return replace(s, '    fParent = NULL;', '    OSSafeReleaseNULL(fParent);')
    if name == 'RavynXHCIPort.h':
        s = replace(s, '#include "XHCI.h"', '#include "XHCI.h"\n#include "sf_usb_lifetime.h"\nclass SFUSBDart;')
        s = replace(s, '    IOService          * fProvider;', '''    struct sf_usb_lifetime fLifetime;
    uint32_t fWorkers;
    bool fDmaLive;
    IOLock *fRemovalLock;
    SFUSBDart *fAppleDart;
    IOBufferMemoryDescriptor *allocDMABuffer(task_t task, IOOptionBits options,
                                            mach_vm_size_t bytes, mach_vm_address_t mask);
    bool startApple(IOService *provider);
    bool startWorker(thread_continue_t entry);
    IOService          * fProvider;''')
        s = replace(s, '    void stop(IOService *provider) override;', '''    void stop(IOService *provider) override;
    IOReturn callPlatformFunction(const OSSymbol *, bool, void *, void *, void *, void *) override;''')
        return s
    if name == 'Info.plist':
        s = replace(s, '        <key>A733 DWC3</key>', '''        <key>Apple prepared DWC3</key>
        <dict>
            <key>CFBundleIdentifier</key><string>com.ravynos.driver.RavynXHCIPort</string>
            <key>IOClass</key><string>RavynXHCIPort</string>
            <key>IONameMatch</key><string>pd,apple-dwc3-host</string>
            <key>IOProviderClass</key><string>IOPlatformDevice</string>
            <key>IOProbeScore</key><integer>5000</integer>
        </dict>
        <key>A733 DWC3</key>''')
        return replace(s, '1.0.0, Copyright (C) 2026 ravynOS Project. All rights reserved.', '1.0.0, ravynOS MIT; Apple DMA backend GPL-2.0, Copyright (C) 2021 The Asahi Linux Contributors; ARM Limited 2013, 2014, 2015; Samsung Electronics 2011, 2016. See NOTICE.')
    if name == 'CMakeLists.txt':
        return s + '''
# Apple DMA backend retains its GPL-2.0 notices in source and NOTICE.
target_sources(RavynXHCIPort.kext PRIVATE sf_usb_dart.cpp sf_dart_table.c sf_soc.cpp)
'''
    assert name == 'RavynXHCIPort.cpp'
    s = replace(s, '#include "RavynXHCIPort.h"', '#include "RavynXHCIPort.h"\n#include "sf_usb_dart.h"')
    # Replace every payload/ring address, leaving only physical MMIO map logs.
    import re
    s = re.sub(r'\(in \? sr.bulkInRingMem : sr.bulkOutRingMem\)->getPhysicalAddress\(\)', r'dmaAddress(in ? sr.bulkInRingMem : sr.bulkOutRingMem)', s)
    s = s.replace('(mem)->getPhysicalAddress()', 'dmaAddress(mem)')
    s = re.sub(r'([\w.]+(?:->\w+)?)->getPhysicalAddress\(\)', lambda m: m[0] if m[1] == 'fBARMap' else 'dmaAddress(' + m[1] + ')', s)
    assert s.count('getPhysicalAddress()') == 2
    assert s.count('IOBufferMemoryDescriptor::inTaskWithPhysicalMask(') == 13
    s = s.replace('IOBufferMemoryDescriptor::inTaskWithPhysicalMask(', 'allocDMABuffer(')
    pos = s.index('#define kAssignedAddrKey')
    s = s[:pos] + '''static UInt64 dmaAddress(IOBufferMemoryDescriptor *mem)
{
    SFUSBDMABuffer *apple = OSDynamicCast(SFUSBDMABuffer, mem);
    return apple ? apple->dmaAddress() : mem->getPhysicalAddress();
}

IOBufferMemoryDescriptor *RavynXHCIPort::allocDMABuffer(task_t task, IOOptionBits options,
                                                      mach_vm_size_t bytes, mach_vm_address_t mask)
{
    if (!fAppleDart) return IOBufferMemoryDescriptor::inTaskWithPhysicalMask(task, options, bytes, mask);
    UInt64 alignment = mask & (~mask + 1);
    if (alignment < SF_DART_PAGE) alignment = SF_DART_PAGE;
    return SFUSBDMABuffer::allocate(fAppleDart, bytes, alignment);
}

bool RavynXHCIPort::startApple(IOService *provider)
{
    if (provider->callPlatformFunction("SFAppleUSBDart", false, &fAppleDart, NULL, NULL, NULL)
        != kIOReturnSuccess || !fAppleDart) return false;
    fBARMap = provider->mapDeviceMemoryWithIndex(0, kIOMapInhibitCache);
    if (!fBARMap || fBARMap->getLength() < 0x10000) return false;
    volatile UInt32 *core = (volatile UInt32 *)fBARMap->getVirtualAddress();
    if ((core[0xc120 / 4] & 0xffff0000) != 0x33310000 ||
        (core[0xc110 / 4] & (3u << 12)) != (1u << 12)) return false;
    fDMAMask = 0x0000000fffffc000ULL;
    return true;
}

bool RavynXHCIPort::startWorker(thread_continue_t entry)
{
    retain();
    __atomic_add_fetch(&fWorkers, 1, __ATOMIC_ACQ_REL);
    thread_t t = THREAD_NULL;
    if (kernel_thread_start(entry, this, &t) == KERN_SUCCESS) {
        thread_deallocate(t);
        return true;
    }
    __atomic_sub_fetch(&fWorkers, 1, __ATOMIC_RELEASE);
    release();
    return false;
}

''' + s[pos:]
    s = replace(s, '    if (!pci && !isCompatible', '    if (!pci && strcmp(provider->getName(), "pd,apple-dwc3-host") != 0 && !isCompatible')
    s = replace(s, '    fPCI = OSDynamicCast(IOPCIDevice, provider);', '''    sf_usb_lifetime_init(&fLifetime);
    SFUSBOperation starting(&fLifetime);
    fRemovalLock = IOLockAlloc();
    if (!fRemovalLock) return false;
    fPCI = OSDynamicCast(IOPCIDevice, provider);''')
    s = replace(s, '    } else if (isCompatible(provider, kDWC3Compatible)) {', '''    } else if (strcmp(provider->getName(), "pd,apple-dwc3-host") == 0) {
        if (!startApple(provider)) return false;
    } else if (isCompatible(provider, kDWC3Compatible)) {''')
    s = replace(s, '    fProvider = provider;', '    fProvider = provider;\n    fProvider->retain();')
    s = replace(s, '    fProvider->retain();\n    if (fPCI)', '    if (fPCI)')
    # Controller may begin fetching DMA immediately when DCBAAP/CRCR/ERST are written.
    s = replace(s, '    if (!setupDCBAA()', '    fDmaLive = true;\n    if (!setupDCBAA()')
    s = replace(s, '    registerService();\n\n    fHotplugRunning', '    fHotplugRunning')
    a = s.index('    thread_t hpThread = THREAD_NULL;')
    b = s.index('    // a restart', a)
    s = s[:a] + '''    if (!startWorker((thread_continue_t)&RavynXHCIPort::hotplugThread)) {
        XHCI_Log("failed to start hotplug poll thread");
        fHotplugRunning = false;
    }
''' + s[b:]
    s = replace(s, '    fRestartNotifier = registerPrioritySleepWakeInterest(&RavynXHCIPort::restartHandler, this, NULL);', '    fRestartNotifier = registerPrioritySleepWakeInterest(&RavynXHCIPort::restartHandler, this, NULL);\n    registerService();')
    old = '''        thread_t t;
        fBulkThreadStarted = true;
        if (kernel_thread_start((thread_continue_t)&RavynXHCIPort::bulkThread, this, &t) == KERN_SUCCESS)
            thread_deallocate(t);
        else
            fBulkThreadStarted = false;'''
    s = replace(s, old, '''        fBulkThreadStarted = startWorker((thread_continue_t)&RavynXHCIPort::bulkThread);''', 2)
    s = replace(s, '    if (!fBulkThreadStarted) {',
                '    if (!__atomic_exchange_n(&fBulkThreadStarted, true, __ATOMIC_ACQ_REL)) {', 2)
    # Admission covers every method reached from the USB bus and mass-storage nub,
    # plus event callbacks. Internal nested calls can fail promptly after close.
    guards = {
        'controlTransfer': 'return false;', 'interruptTransfer': 'return kIOReturnOffline;',
        'abortInterruptEndpoint': 'return kIOReturnOffline;', 'configureInterruptInEndpoint': 'return false;',
        'openBulkEndpoint': 'return false;', 'openIsochEndpoint': 'return false;',
        'submitBulk': 'return kIOReturnOffline;', 'submitIsoch': 'return kIOReturnOffline;',
        'bulkSync': 'return kIOReturnOffline;', 'abortBulk': 'return kIOReturnOffline;',
        'botTransfer': 'return kIOReturnOffline;', 'doCommand': 'return false;',
        'serviceEventRing': 'return;', 'interruptOccurred': 'return;',
        'microframeIndex': 'return 0;', 'logBOTState': 'return;'
    }
    for method, failure in guards.items():
        pattern = r'(RavynXHCIPort::' + method + r'\([^{}]*\)\s*\{)'
        s, n = re.subn(pattern, r'\1\n    SFUSBOperation operation(&fLifetime);\n    if (!operation) ' + failure, s)
        assert n == 1, (method, n)
    s = replace(s, '    while (fHotplugRunning) {', '''    while (fHotplugRunning) {
        SFUSBOperation operation(&fLifetime);
        if (!operation) break;''')
    s = replace(s, '    while (!self->fBulkStop) {', '''    while (!self->fBulkStop) {
        SFUSBOperation operation(&self->fLifetime);
        if (!operation) break;''')
    s = replace(s, '    thread_terminate(current_thread());', '''    __atomic_sub_fetch(&self->fWorkers, 1, __ATOMIC_RELEASE);
    self->release();
    thread_terminate(current_thread());''', 2)
    # Existing synchronous loops stop promptly when close starts.
    a=s.index('bool RavynXHCIPort::doCommand('); b=s.index('void RavynXHCIPort::scanPorts()',a)
    part=s[a:b].replace('    for (;;) {', '    for (;;) {\n        if (sf_usb_lifetime_closed(&fLifetime)) break;',1)
    part=part.replace('    if (!ok)\n        abortCommandRing', '    if (!ok && !sf_usb_lifetime_closed(&fLifetime))\n        abortCommandRing')
    part=part.replace('    for (;;) {\n        UInt32 generation', '    for (;;) {\n        if (sf_usb_lifetime_closed(&fLifetime)) return false;\n        UInt32 generation')
    s=s[:a]+part+s[b:]
    s = replace(s, '    quiesce("stop");\n    super::stop(provider);', '''    quiesce("stop");
    if (fQuiesced) {
        if (fDCBAA && fEventLock)
            for (UInt32 slot = 1; slot < 64; ++slot) freeSlotResources(slot);
        for (UInt32 p = 1; p <= fMaxPorts && p <= 64; ++p) disconnectUSBDevices(p, 0, 0);
        // Break our ownership cycles; outstanding class clients retain the bus
        // and port until they have observed offline and finished their calls.
        OSSafeReleaseNULL(fUSBBus);
    }
    super::stop(provider);''')
    s = replace(s, '    fDmaLive = true;', '    fDmaLive = true;\n    fProvider->setProperty("sf-usb-dma-live", true);')
    a=s.index('void RavynXHCIPort::quiesce('); b=s.index('IOReturn RavynXHCIPort::restartHandler',a)
    s=s[:a]+'''// Closing admission and draining CPU access precede hardware halt. A timeout
// leaves DMA and its mappings alive; the parent must not reset the PHY then.
void RavynXHCIPort::quiesce(const char *why)
{
    if (!fRemovalLock) return;
    IOLockLock(fRemovalLock);
    if (fQuiesced) { IOLockUnlock(fRemovalLock); return; }
    sf_usb_lifetime_close(&fLifetime);
    fHotplugRunning = false;
    fBulkStop = true;
    if (fInterruptSource) fInterruptSource->disable();
    if (fEventWaitLock) {
        IOLockLock(fEventWaitLock);
        ++fEventGeneration;
        IOLockWakeup(fEventWaitLock, &fEventWaitChannel, false);
        IOLockWakeup(fEventWaitLock, &fHotplugWaitChannel, false);
        IOLockUnlock(fEventWaitLock);
    }
    unsigned wait;
    for (wait = 0; wait < 15000; ++wait) {
        if (!sf_usb_lifetime_active(&fLifetime) &&
            !__atomic_load_n(&fWorkers, __ATOMIC_ACQUIRE)) break;
        IOSleep(1);
    }
    if (wait == 15000) {
        IOLog("RavynXHCIPort: %s drain timed out; retaining controller and DMA\\n", why);
        IOLockUnlock(fRemovalLock);
        return;
    }
    if (fOpRegs) {
        opWrite32(XHCI_USBCMD, opRead32(XHCI_USBCMD) & ~(XHCI_USBCMD_RS | XHCI_USBCMD_INTE));
        for (wait = 0; wait < 2000 && !(opRead32(XHCI_USBSTS) & XHCI_USBSTS_HCH); ++wait) IOSleep(1);
        if (wait == 2000) { IOLockUnlock(fRemovalLock); return; }
        opWrite32(XHCI_USBCMD, opRead32(XHCI_USBCMD) | XHCI_USBCMD_HCRST);
        for (wait = 0; wait < 2000 && ((opRead32(XHCI_USBCMD) & XHCI_USBCMD_HCRST) ||
             (opRead32(XHCI_USBSTS) & XHCI_USBSTS_CNR)); ++wait) IOSleep(1);
        if (wait == 2000) { IOLockUnlock(fRemovalLock); return; }
    }
    fDmaLive = false;
    if (fProvider) fProvider->setProperty("sf-usb-dma-live", false);
    fQuiesced = true;
    setProperty("sf-dma-drained", true);
    IOLockUnlock(fRemovalLock);
    IOLog("RavynXHCIPort: drained and reset for %s\\n", why);
}

IOReturn RavynXHCIPort::callPlatformFunction(const OSSymbol *name, bool wait,
                                            void *a, void *b, void *c, void *d)
{
    if (name && name->isEqualTo("SFAppleUSBDrain")) {
        quiesce("cable removal");
        return fQuiesced ? kIOReturnSuccess : kIOReturnBusy;
    }
    return super::callPlatformFunction(name, wait, a, b, c, d);
}

'''+s[b:]
    s=replace(s, '    fBulkStop = true;\n    for (int i = 0; i < 16;', '''    if (fDmaLive && !fQuiesced) {
        IOLog("RavynXHCIPort: refusing to free live DMA after failed start/stop\\n");
        return;
    }
    if (fDCBAA && fEventLock)
        for (UInt32 slot = 1; slot < 64; ++slot) freeSlotResources(slot);
    OSSafeReleaseNULL(fDCBAAMem);
    OSSafeReleaseNULL(fCmdRingMem);
    OSSafeReleaseNULL(fEventRingMem);
    OSSafeReleaseNULL(fERSTMem);
    OSSafeReleaseNULL(fScratchpadArrayMem);
    if (fScratchpadBufMem) {
        for (UInt32 i = 0; i < fScratchpadCount; ++i) OSSafeReleaseNULL(fScratchpadBufMem[i]);
        IOFree(fScratchpadBufMem, fScratchpadCount * sizeof(*fScratchpadBufMem));
    }
    if (fRemovalLock) { IOLockFree(fRemovalLock); fRemovalLock = NULL; }
    fBulkStop = true;
    for (int i = 0; i < 16;''')
    return s


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('source',type=Path); ap.add_argument('output',type=Path)
    args=ap.parse_args(); chunks=[]
    for name in ['RavynXHCIPort.h','RavynXHCIPort.cpp','CMakeLists.txt','Info.plist','RavynXHCIUSBBus.h','RavynXHCIUSBBus.cpp','RavynXHCIMassStorageDisk.cpp']:
        p=args.source / name; old=p.read_text(); new=transform(name,old)
        path='src/Kernel/Extensions/RavynXHCIPort/'+name
        chunks.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True), 'a/'+path,'b/'+path))
    args.output.write_text(''.join(chunks))

if __name__ == '__main__': main()
