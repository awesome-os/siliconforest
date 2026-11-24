#ifndef SF_M1_SPI_HID_H
#define SF_M1_SPI_HID_H
#include "IOHIDDevice.h"
#include "SFM1KeyboardHW.h"
#include "native_protocol/dl_spi_hid.h"
#include "m1kbd_hid.h"
/* Layout first: it defines and undefines its own one-argument assert macro. */
#include "observation_layout.h"
#include "m1tp_enum.h"
#include "child_state.h"
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>

class SFM1SPITrackpad;
class OSData;
class SFM1SPIHID : public IOHIDDevice {
    OSDeclareDefaultStructors(SFM1SPIHID);
public:
    bool init(OSDictionary *properties=nullptr) APPLE_KEXT_OVERRIDE;
    IOService *probe(IOService *, SInt32 *) APPLE_KEXT_OVERRIDE;
    bool start(IOService *) APPLE_KEXT_OVERRIDE;
    void stop(IOService *) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
    IOWorkLoop *getWorkLoop() const APPLE_KEXT_OVERRIDE;
    bool handleStart(IOService *) APPLE_KEXT_OVERRIDE;
    void handleStop(IOService *) APPLE_KEXT_OVERRIDE;
    bool terminate(IOOptionBits options=0) APPLE_KEXT_OVERRIDE;
    bool handleOpen(IOService *, IOOptionBits, void *) APPLE_KEXT_OVERRIDE;
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) APPLE_KEXT_OVERRIDE;
    /* Recursive sink clear: no waiting or dispatch-drain sleep. */
    void retireChild(SFM1SPITrackpad *expected);
    IOReturn newReportDescriptor(IOMemoryDescriptor **) const APPLE_KEXT_OVERRIDE;
    OSString *newTransportString() const APPLE_KEXT_OVERRIDE;
    OSString *newProductString() const APPLE_KEXT_OVERRIDE;
    OSNumber *newVendorIDNumber() const APPLE_KEXT_OVERRIDE;
    OSNumber *newProductIDNumber() const APPLE_KEXT_OVERRIDE;
    OSNumber *newVersionNumber() const APPLE_KEXT_OVERRIDE;
    OSNumber *newLocationIDNumber() const APPLE_KEXT_OVERRIDE;
    OSNumber *newCountryCodeNumber() const APPLE_KEXT_OVERRIDE;
private:
    SFM1KeyboardHW hardware;
    struct dlsp_state wire;  // IOService heap object, never kernel stack
    uint8_t packet[DLSP_PACKET_BYTES];
    uint8_t descriptor[DLSP_DESCRIPTOR_BYTES];
    size_t descriptorLength;
    struct m1kbd_identity identity;
    /* T1 trackpad enumeration snapshot: heap member, zeroed in init, never a
     * stack copy. Read-only diagnostic data; not a pointer device or input. */
    struct m1tp_observation trackpadEnumeration;
    struct m1tp_interface_info trackpadInterface;
    uint8_t nextID, usedRequestKeys;
    IOWorkLoop *transportLoop;
    IOCommandGate *transportGate;
    IOTimerEventSource *pollTimer;
    bool stopping, published, touchedHardware, gateAdded, timerAdded;
    uint64_t packetsRead, reportsDelivered, otherMessages;
    struct m1kbd_observation lastObservation;
    uint64_t lastCountersUS;
    bool countersDirty, requestPending;
    uint8_t pendingReport, pendingSubdevice, requestID;
    bool t2Enabled, childInitialized, childAttached, childStarted;
    bool privateStopRunning, privateStopDone;
    volatile UInt32 t2Phase;
    SFM1SPITrackpad *childOwned, *childSink;
    IOService *t2Provider;
    OSData *t2CommitData;
    struct dl_m1_trackpad_child_state_v1 childState;
    uint32_t t2Dispatches;
    uint64_t childObserved, childRejected, childSubmitted;
    bool t2Committed() const;
    bool prepareChild();
    void rollbackChild();
    bool startT2(IOService *);
    static IOReturn commitT2Gated(OSObject *, void *, void *, void *, void *);
    static IOReturn stopT2Gated(OSObject *, void *, void *, void *, void *);
    static IOReturn retireChildGated(OSObject *, void *, void *, void *, void *);
    bool installChildState(OSData *);
    bool publishChildState();
    void removeChildState();
    void childFault(int32_t);
    void routeTrackpad(const struct dlsp_message *);
    static void poll(OSObject *, IOTimerEventSource *);
    static IOReturn startGated(OSObject *, void *, void *, void *, void *);
    static IOReturn stopGated(OSObject *, void *, void *, void *, void *);
    bool startHardware();
    bool enumerate();
    bool enumerateTrackpad();
    bool preserveTrackpadEnumeration(IOService *provider);
    int receive(struct dlsp_message *);
    bool waitResponse(uint8_t report, uint8_t subdevice, uint16_t responseLength, struct dlsp_message *);
    void fail(int error, const char *phase);
    void publishCounters(bool force);
    void quiesce();
    void disposeTransport();
};
#endif
