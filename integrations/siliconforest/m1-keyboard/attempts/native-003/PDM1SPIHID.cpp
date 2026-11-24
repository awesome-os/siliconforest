#include "PDM1SPIHID.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/libkern.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>

#define super IOHIDDevice
OSDefineMetaClassAndStructors(PDM1SPIHID,IOHIDDevice);

/* The experimental module cannot be unloaded, including on start failure.
 * This is an explicit failure, not evidence of callback quiescence. */
extern "C" kern_return_t pd_m1kbd_stop(kmod_info_t *, void *) { return KERN_FAILURE; }

static bool recordBytes(IORegistryEntry *entry, const char *key, const void *bytes, unsigned length)
{
    OSData *data=OSData::withBytes(bytes,length);
    if (!data) return false;
    bool result=entry->setProperty(key,data);
    data->release();
    return result;
}
bool PDM1SPIHID::init(OSDictionary *properties)
{
    transportLoop=nullptr; transportGate=nullptr; pollTimer=nullptr;
    stopping=false; published=false; touchedHardware=false; gateAdded=false; timerAdded=false;
    descriptorLength=0; bzero(&identity,sizeof(identity)); nextID=0;
    packetsRead=0; reportsDelivered=0; otherMessages=0;
    bzero(&lastObservation,sizeof(lastObservation)); lastCountersUS=0;
    countersDirty=false; responseExpected=false;
    expectedReport=0; expectedSubdevice=0; expectedID=0;
    bzero(packet,sizeof(packet)); bzero(descriptor,sizeof(descriptor)); dlsp_init(&wire);
    return super::init(properties);
}
IOService *PDM1SPIHID::probe(IOService *provider, SInt32 *score)
{
    int enabled=0;
    if (!PE_parse_boot_argn("pdm1kbd",&enabled,sizeof(enabled)) || enabled!=1) return nullptr;
    return super::probe(provider,score);
}
IOWorkLoop *PDM1SPIHID::getWorkLoop() const { return transportLoop; }
bool PDM1SPIHID::start(IOService *provider)
{
    transportLoop=IOWorkLoop::workLoop();
    if (!transportLoop) return false;
    transportGate=IOCommandGate::commandGate(this);
    /* WorkLoop delivery avoids passive thread-call actions waiting outside
     * the workloop gate. cancelTimeout joins the signal callout; removal
     * then prevents checkForWork from invoking a stale owner. */
    pollTimer=IOTimerEventSource::timerEventSource(kIOTimerEventSourceOptionsPriorityWorkLoop,this,poll);
    if (!transportGate || !pollTimer) { disposeTransport(); return false; }
    if (transportLoop->addEventSource(transportGate)!=kIOReturnSuccess) {
        disposeTransport(); return false;
    }
    gateAdded=true;
    if (transportLoop->addEventSource(pollTimer)!=kIOReturnSuccess) {
        disposeTransport(); return false;
    }
    timerAdded=true;
    /* IOHIDDevice calls handleStart before asking for the real descriptor.
     * No timer is armed and report operations are Unsupported during init. */
    if (!super::start(provider)) { disposeTransport(); return false; }
    published=true;
    if (pollTimer->setTimeoutMS(2)!=kIOReturnSuccess) {
        super::stop(provider); disposeTransport(); return false;
    }
    setProperty("PDM1KeyboardPhase","descriptor-published-awaiting-input");
    return true;
}
bool PDM1SPIHID::handleStart(IOService *provider)
{
    if (!super::handleStart(provider)) return false;
    return transportGate && transportGate->runAction(startGated)==kIOReturnSuccess;
}
IOReturn PDM1SPIHID::startGated(OSObject *owner, void *, void *, void *, void *)
{
    PDM1SPIHID *self=OSDynamicCast(PDM1SPIHID,owner);
    if (!self || self->stopping) return kIOReturnNotReady;
    return self->startHardware()?kIOReturnSuccess:kIOReturnError;
}
bool PDM1SPIHID::startHardware()
{
    int e=hardware.open();
    if (e) { fail(e,"board-or-map"); return false; }
    touchedHardware=true;
    e=hardware.start();
    const struct m1kbd_hw *h=hardware.state();
    if (!recordBytes(this,"PDM1KeyboardPowerBefore",h->power_before,sizeof(h->power_before)) ||
        !recordBytes(this,"PDM1KeyboardGPIOBefore",h->gpio_before,sizeof(h->gpio_before))) {
        fail(M1KBD_IO,"observation-allocation"); return false;
    }
    if (e) { fail(e,"controller-start"); return false; }
    setProperty("PDM1KeyboardPhase","controller-ready");
    if (!enumerate()) return false;
    setProperty("HIDDefaultBehavior",kOSBooleanTrue);
    setProperty("Built-In",kOSBooleanTrue);
    IOLog("PDM1SPIHID: CRC-checked J293 keyboard descriptor acquired (%u bytes); input not yet observed\n",
          (unsigned)descriptorLength);
    return true;
}
int PDM1SPIHID::receive(struct dlsp_message *message)
{
    int e=hardware.readPacket(packet);
    if (e==M1KBD_NOT_READY) return DLSP_WAIT;
    if (e) { fail(e,"read-packet"); return DLSP_INVALID; }
    packetsRead++;
    countersDirty=true;
    e=dlsp_push(&wire,packet,sizeof(packet),message);
    m1kbd_observe(packet,sizeof(packet),e==DLSP_MESSAGE?message:nullptr,
                  (unsigned)wire.last_error,&lastObservation);
    if (e==DLSP_INVALID) {
        setProperty("PDM1KeyboardProtocolError",(uint64_t)wire.last_error,32);
        fail(M1KBD_INVALID,"packet-or-message");
    }
    return e;
}
bool PDM1SPIHID::waitResponse(uint8_t report, uint8_t subdevice, uint16_t responseLength,
                            struct dlsp_message *message)
{
    uint8_t id=nextID++;
    responseExpected=true; expectedReport=report; expectedSubdevice=subdevice; expectedID=id;
    if (dlsp_request(0xd0,0x20,report,subdevice,id,responseLength,nullptr,0,packet)!=0) {
        fail(M1KBD_INVALID,"request-encode"); return false;
    }
    int e=hardware.writePacket(packet);
    if (e) { fail(e,"write-request"); return false; }
    uint64_t start=PDM1KeyboardHW::clockUS();
    for (unsigned polls=0;polls<1000;polls++) {
        if (PDM1KeyboardHW::clockUS()-start>=1000000) break;
        e=receive(message);
        if (e==DLSP_INVALID) return false;
        if (e==DLSP_BOOT) { fail(M1KBD_STATE,"unexpected-reboot"); return false; }
        if (e==DLSP_MESSAGE) {
            if (message->flags==0x20) { otherMessages++; continue; }
            if (!m1kbd_response_matches(message,report,subdevice,id)) {
                fail(M1KBD_STATUS,"response-identity"); return false;
            }
            responseExpected=false;
            return true;
        }
        IOSleep(1);
    }
    fail(M1KBD_TIMEOUT,"response-timeout"); return false;
}
bool PDM1SPIHID::enumerate()
{
    struct dlsp_message message;
    bool boot=false;
    uint64_t start=PDM1KeyboardHW::clockUS();
    for (unsigned polls=0;polls<1000;polls++) {
        if (PDM1KeyboardHW::clockUS()-start>=1000000) break;
        int e=receive(&message);
        if (e==DLSP_INVALID) return false;
        if (e==DLSP_BOOT) { boot=true; break; }
        if (e==DLSP_MESSAGE) { fail(M1KBD_STATE,"message-before-boot"); return false; }
        IOSleep(1);
    }
    if (!boot) { fail(M1KBD_TIMEOUT,"boot-timeout"); return false; }
    setProperty("PDM1KeyboardPhase","boot-notification-verified");
    if (!waitResponse(1,0xd0,0,&message)) return false;
    /* Device metadata comes from the controller, not our selected descriptor.
     * The J293 Linux observation identifies Apple 05ac:0341. */
    if (m1kbd_device_info(message.payload,message.payload_length,&identity)) {
        IOLog("PDM1SPIHID: device metadata bytes=%u\n",(unsigned)message.payload_length);
        fail(M1KBD_BOARD,"device-metadata"); return false;
    }
    if (!waitResponse(2,1,512,&message)) return false;
    if (m1kbd_interface_info(message.payload,message.payload_length,&identity)) {
        IOLog("PDM1SPIHID: keyboard interface metadata bytes=%u\n",(unsigned)message.payload_length);
        fail(M1KBD_INVALID,"interface-metadata"); return false;
    }
    if (!waitResponse(0x10,1,512,&message)) return false;
    if (!message.payload_length || message.payload_length>sizeof(descriptor)) {
        fail(M1KBD_INVALID,"descriptor-length"); return false;
    }
    memcpy(descriptor,message.payload,message.payload_length);
    descriptorLength=message.payload_length;
    return true;
}
void PDM1SPIHID::poll(OSObject *owner, IOTimerEventSource *)
{
    PDM1SPIHID *self=OSDynamicCast(PDM1SPIHID,owner);
    if (!self || self->stopping || !self->published) return;
    struct dlsp_message message;
    /* A bounded batch lets other workloop users run, even with ready held low.
     * Partial messages remain in the bounded heap reassembly buffer. */
    for (unsigned budget=0;budget<8;budget++) {
        int e=self->receive(&message);
        if (e==DLSP_INVALID) return;
        if (e==DLSP_BOOT) { self->fail(M1KBD_STATE,"runtime-device-reboot"); return; }
        if (e==DLSP_WAIT) break;
        int eligible=m1kbd_keyboard_input(&message,self->identity.max_input);
        if (eligible<0) { self->fail(M1KBD_INVALID,"input-size"); return; }
        if (eligible) {
            IOBufferMemoryDescriptor *report=IOBufferMemoryDescriptor::withBytes(
                message.payload,message.payload_length,kIODirectionNone);
            if (!report) { self->fail(M1KBD_IO,"input-allocation"); return; }
            IOReturn kr=self->handleReport(report,kIOHIDReportTypeInput);
            report->release();
            if (kr!=kIOReturnSuccess) { self->fail((int)kr,"hid-input"); return; }
            self->reportsDelivered++;
            if (self->reportsDelivered==1) {
                self->publishCounters(true);
                IOLog("PDM1SPIHID: first device-1 input report accepted by IOHID; physical typing not yet verified\n");
            }
        } else {
            /* Management/trackpad/aux reports are consumed but never relabeled
             * as keyboard events or evidence of supported native trackpad. */
            self->otherMessages++;
        }
    }
    self->publishCounters(false);
    if (!self->stopping && self->pollTimer->setTimeoutMS(2)!=kIOReturnSuccess)
        self->fail(M1KBD_IO,"timer-rearm");
}
void PDM1SPIHID::publishCounters(bool force)
{
    if (!force && !countersDirty) return;
    uint64_t t=PDM1KeyboardHW::clockUS();
    if (!force && lastCountersUS && t-lastCountersUS<100000) return;
    setProperty("PDM1KeyboardPackets",packetsRead,64);
    setProperty("PDM1KeyboardReports",reportsDelivered,64);
    setProperty("PDM1KeyboardOtherMessages",otherMessages,64);
    setProperty("PDM1KeyboardCountersUS",t,64);
    lastCountersUS=t; countersDirty=false;
}
void PDM1SPIHID::fail(int error, const char *phase)
{
    stopping=true;
    setProperty("PDM1KeyboardPhase",phase);
    setProperty("PDM1KeyboardError",(uint64_t)(int64_t)error,64);
    publishCounters(true);
    (void)recordBytes(this,"PDM1KeyboardLastWireHeaderV1",&lastObservation,sizeof(lastObservation));
    const struct m1kbd_observation *o=&lastObservation;
    IOLog("PDM1SPIHID: last completed packet present=%u flags=%02x device=%02x offset=%u remain=%u length=%u parser_error=%u\n",
          o->packet_available,o->packet_flags,o->packet_device,o->offset,o->remain,o->length,o->parser_error);
    IOLog("PDM1SPIHID: last decoded message present=%u flags=%02x device=%02x command=%02x report=%02x subdevice=%02x id=%u response_length=%u payload_length=%u\n",
          o->message_available,o->flags,o->device,o->command,o->report,o->subdevice,o->id,o->response_length,o->payload_length);
    if (responseExpected)
        IOLog("PDM1SPIHID: expected response device=d0 command=20 report=%02x subdevice=%02x id=%u\n",
              (unsigned)expectedReport,(unsigned)expectedSubdevice,(unsigned)expectedID);
    IOLog("PDM1SPIHID: stopped at %s, error %d; no automatic retry\n",phase,error);
    /* During init no producer exists. During a callback the workloop gate
     * serializes this stop; no callback unmaps resources or frees itself. */
    if (touchedHardware && !hardware.state()->busy) {
        /* Keep mappings until quiesce; core stop is synchronous. */
        struct m1kbd_hw *h=const_cast<struct m1kbd_hw *>(hardware.state());
        int e=m1kbd_hw_stop(h);
        if (e) setProperty("PDM1KeyboardCleanupError",(uint64_t)(int64_t)e,64);
    }
}
IOReturn PDM1SPIHID::stopGated(OSObject *owner, void *, void *, void *, void *)
{
    PDM1SPIHID *self=OSDynamicCast(PDM1SPIHID,owner);
    if (!self) return kIOReturnBadArgument;
    self->stopping=true; self->published=false;
    if (self->pollTimer) self->pollTimer->cancelTimeout();
    return kIOReturnSuccess;
}
void PDM1SPIHID::quiesce()
{
    if (transportGate && gateAdded) {
        IOReturn kr=transportGate->runAction(stopGated);
        if (kr!=kIOReturnSuccess) panic("PDM1SPIHID: refusing teardown without workloop quiescence");
    }
    else { stopping=true; published=false; }
    if (pollTimer && timerAdded) {
        pollTimer->cancelTimeout();
        transportLoop->removeEventSource(pollTimer); timerAdded=false;
    }
    if (hardware.state()->busy) panic("PDM1SPIHID: live transport after workloop quiescence");
    int e=hardware.closeAfterQuiesce();
    if (e) setProperty("PDM1KeyboardCleanupError",(uint64_t)(int64_t)e,64);
}
void PDM1SPIHID::handleStop(IOService *provider)
{ quiesce(); super::handleStop(provider); }
void PDM1SPIHID::stop(IOService *provider)
{ super::stop(provider); disposeTransport(); }
void PDM1SPIHID::disposeTransport()
{
    quiesce();
    if (transportGate && gateAdded) { transportLoop->removeEventSource(transportGate); gateAdded=false; }
    if (pollTimer) { pollTimer->release(); pollTimer=nullptr; }
    if (transportGate) { transportGate->release(); transportGate=nullptr; }
    if (transportLoop) { transportLoop->release(); transportLoop=nullptr; }
}
void PDM1SPIHID::free() { disposeTransport(); super::free(); }
bool PDM1SPIHID::terminate(IOOptionBits options)
{
    (void)options;
    /* IOResources is permanent. Dynamic detach is unsupported in this
     * first candidate; start-failure cleanup still calls stop directly. */
    return false;
}
IOReturn PDM1SPIHID::newReportDescriptor(IOMemoryDescriptor **out) const
{
    if (!out) return kIOReturnBadArgument;
    *out=nullptr;
    if (!descriptorLength) return kIOReturnNotReady;
    *out=IOBufferMemoryDescriptor::withBytes(descriptor,descriptorLength,kIODirectionNone);
    return *out?kIOReturnSuccess:kIOReturnNoMemory;
}
OSString *PDM1SPIHID::newTransportString() const { return OSString::withCString("SPI"); }
OSString *PDM1SPIHID::newProductString() const { return OSString::withCString("Apple SPI Keyboard (J293)"); }
OSNumber *PDM1SPIHID::newVendorIDNumber() const { return OSNumber::withNumber(identity.vendor,32); }
OSNumber *PDM1SPIHID::newProductIDNumber() const { return OSNumber::withNumber(identity.product,32); }
OSNumber *PDM1SPIHID::newVersionNumber() const { return OSNumber::withNumber(identity.version,32); }
OSNumber *PDM1SPIHID::newLocationIDNumber() const { return OSNumber::withNumber(0x3510c000,32); }
OSNumber *PDM1SPIHID::newCountryCodeNumber() const { return OSNumber::withNumber(identity.country,32); }
