#include "SFM1SPITrackpad.h"
#include "SFM1SPIHID.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/libkern.h>

#define super IOHIDDevice
OSDefineMetaClassAndStructors(SFM1SPITrackpad,IOHIDDevice);

bool SFM1SPITrackpad::initWithSnapshot(const uint8_t *bytes, size_t length,
    uint16_t vid, uint16_t pid, uint16_t ver, uint8_t ctry)
{
    phase=0; privateStopRunning=false; privateStopDone=false; baseStarted=false;
    descriptorLength=0;
    if (!bytes || !length || length>sizeof(descriptor) || !super::init()) return false;
    bzero(descriptor,sizeof(descriptor));
    bcopy(bytes,descriptor,length); descriptorLength=length;
    vendor=vid; product=pid; version=ver; country=ctry;
    OSNumber *iface=OSNumber::withNumber(2,32);
    bool okay=iface && setProperty(DL_M1_TP_INTERFACE_PROPERTY,iface) &&
        getProperty(DL_M1_TP_INTERFACE_PROPERTY)==iface;
    if (iface) iface->release();
    return okay && setProperty(kIOHIDRegisterServiceKey,kOSBooleanFalse) &&
        getProperty(kIOHIDRegisterServiceKey)==kOSBooleanFalse &&
        setProperty("Built-In",kOSBooleanTrue) &&
        setProperty("HIDDefaultBehavior",kOSBooleanTrue);
}
bool SFM1SPITrackpad::committed() const
{ return __atomic_load_n(&phase,__ATOMIC_ACQUIRE)!=0; }
bool SFM1SPITrackpad::admitting() const
{ return __atomic_load_n(&phase,__ATOMIC_ACQUIRE)==1; }
void SFM1SPITrackpad::markCommitted()
{ __atomic_store_n(&phase,1,__ATOMIC_RELEASE); }
void SFM1SPITrackpad::markRetired()
{
    if (committed()) {
        __atomic_store_n(&phase,2,__ATOMIC_RELEASE);
        /* Even a late stop with no provider argument must invalidate this
         * child's admission record without dynamically resolving a loop. */
        removeProperty(DL_M1_TP_CHILD_STATE_PROPERTY);
    }
}

bool SFM1SPITrackpad::start(IOService *provider)
{
    if (getProperty(kIOHIDRegisterServiceKey)!=kOSBooleanFalse) return false;
    /* Inherited getWorkLoop uses the attached provider only during base start.
     * Base IOHIDDevice retains _workLoop. No dynamic loop lookup after detach. */
    baseStarted=super::start(provider);
    IOLog("SFM1SPITrackpad: descriptor parse/interface allocation base_start=%u bytes=%u (no input proof)\n",
          baseStarted?1u:0u,(unsigned)descriptorLength);
    return baseStarted;
}
void SFM1SPITrackpad::clearProviderSink(IOService *provider)
{
    SFM1SPIHID *parent=OSDynamicCast(SFM1SPIHID,provider);
    if (parent) parent->retireChild(this);
}
bool SFM1SPITrackpad::willTerminate(IOService *provider, IOOptionBits options)
{
    markRetired();
    clearProviderSink(provider);
    return super::willTerminate(provider,options);
}
void SFM1SPITrackpad::stop(IOService *provider)
{
    markRetired();
    /* M2: may already hold the inherited loop's recursive gate. This neither
     * waits for dispatches nor resolves getWorkLoop() after detach. */
    clearProviderSink(provider);
    if (committed()) {
        IOLog("SFM1SPITrackpad: TEARDOWN_DEFERRED committed cohort retained\n");
        return;
    }
    if (privateStopDone || privateStopRunning) return;
    privateStopRunning=true;
    super::stop(provider);
    privateStopRunning=false; privateStopDone=true;
}
void SFM1SPITrackpad::handleStop(IOService *provider)
{
    markRetired(); clearProviderSink(provider);
    if (committed()) return;
    super::handleStop(provider);
}
void SFM1SPITrackpad::free()
{
    if (committed()) {
        IOLog("SFM1SPITrackpad: invariant violation; committed free refused\n");
        return;
    }
    super::free();
}
bool SFM1SPITrackpad::handleOpen(IOService *client, IOOptionBits options, void *argument)
{ return admitting() && !isInactive() && super::handleOpen(client,options,argument); }
IOReturn SFM1SPITrackpad::newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **out)
{ if (out) *out=nullptr; return kIOReturnUnsupported; }
IOReturn SFM1SPITrackpad::newReportDescriptor(IOMemoryDescriptor **out) const
{
    if (!out) return kIOReturnBadArgument;
    *out=IOBufferMemoryDescriptor::withBytes(descriptor,descriptorLength,kIODirectionNone);
    return *out?kIOReturnSuccess:kIOReturnNoMemory;
}
IOReturn SFM1SPITrackpad::getReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits)
{ return kIOReturnUnsupported; }
IOReturn SFM1SPITrackpad::setReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits)
{ return kIOReturnUnsupported; }
IOReturn SFM1SPITrackpad::getReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits, UInt32, IOHIDCompletion *)
{ return kIOReturnUnsupported; }
IOReturn SFM1SPITrackpad::setReport(IOMemoryDescriptor *, IOHIDReportType, IOOptionBits, UInt32, IOHIDCompletion *)
{ return kIOReturnUnsupported; }
OSString *SFM1SPITrackpad::newTransportString() const { return OSString::withCString("SPI"); }
OSString *SFM1SPITrackpad::newProductString() const { return OSString::withCString("Apple SPI Trackpad (J293 passive)"); }
OSNumber *SFM1SPITrackpad::newVendorIDNumber() const { return OSNumber::withNumber(vendor,32); }
OSNumber *SFM1SPITrackpad::newProductIDNumber() const { return OSNumber::withNumber(product,32); }
OSNumber *SFM1SPITrackpad::newVersionNumber() const { return OSNumber::withNumber(version,32); }
OSNumber *SFM1SPITrackpad::newCountryCodeNumber() const { return OSNumber::withNumber(country,32); }
