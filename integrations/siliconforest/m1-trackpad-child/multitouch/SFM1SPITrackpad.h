/* Original passive descriptor-backed child; no transport ownership. */
#ifndef SF_M1_SPI_TRACKPAD_H
#define SF_M1_SPI_TRACKPAD_H
#include "IOHIDDevice.h"
#include "child_state.h"

class SFM1SPIHID;
class SFM1SPITrackpad : public IOHIDDevice {
    OSDeclareDefaultStructors(SFM1SPITrackpad);
    friend class SFM1SPIHID;
public:
    bool initWithSnapshot(const uint8_t *, size_t, uint16_t, uint16_t, uint16_t, uint8_t);
    bool start(IOService *) APPLE_KEXT_OVERRIDE;
    void stop(IOService *) APPLE_KEXT_OVERRIDE;
    void handleStop(IOService *) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
    bool willTerminate(IOService *, IOOptionBits) APPLE_KEXT_OVERRIDE;
    bool handleOpen(IOService *, IOOptionBits, void *) APPLE_KEXT_OVERRIDE;
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) APPLE_KEXT_OVERRIDE;
    IOReturn newReportDescriptor(IOMemoryDescriptor **) const APPLE_KEXT_OVERRIDE;
    IOReturn getReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits) APPLE_KEXT_OVERRIDE;
    IOReturn setReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits) APPLE_KEXT_OVERRIDE;
    IOReturn getReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits, UInt32, IOHIDCompletion *) APPLE_KEXT_OVERRIDE;
    IOReturn setReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits, UInt32, IOHIDCompletion *) APPLE_KEXT_OVERRIDE;
    OSString *newTransportString() const APPLE_KEXT_OVERRIDE;
    OSString *newProductString() const APPLE_KEXT_OVERRIDE;
    OSNumber *newVendorIDNumber() const APPLE_KEXT_OVERRIDE;
    OSNumber *newProductIDNumber() const APPLE_KEXT_OVERRIDE;
    OSNumber *newVersionNumber() const APPLE_KEXT_OVERRIDE;
    OSNumber *newCountryCodeNumber() const APPLE_KEXT_OVERRIDE;
private:
    uint8_t descriptor[DL_M1_TP_DESCRIPTOR_BYTES];
    size_t descriptorLength;  /* 110: the device's own; shorter: the synthesized pointer */
    uint16_t vendor, product, version;
    uint8_t country;
    /* 0 private; 1 committed/admitting; 2 committed/retired. Atomic reads in
     * handleOpen avoid acquiring the workloop under the arbitration lock. */
    volatile UInt32 phase;
    bool privateStopRunning, privateStopDone, baseStarted;
    void clearProviderSink(IOService *);
    bool committed() const;
    bool admitting() const;
    void markCommitted();
    void markRetired();
};
#endif
