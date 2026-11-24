// SPDX-License-Identifier: GPL-2.0
/*
 * J293 CD321x transport using the existing Linux PA Semi I2C port.
 * Register reads follow drivers/usb/typec/tipd/core.c (linux-aurora 6a08a08e):
 * Copyright (C) 2017, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 * Copyright (C) The Asahi Linux Contributors
 *
 * Port 1 only: perform the Linux S0/SSPS initialization when necessary,
 * then acknowledge observed events and sample state on the existing 1 Hz
 * workloop. Install Linux's CD3217 interrupt mask at startup; no GPIO/AIC
 * enable, role swap, firmware update, mux write or port-0 transaction is sent.
 */
#include "sf_i2c.h"
#include "sf_cd321x.h"
#include "sf_cd321x_power.h"
#include "sf_cd321x_control.h"
#include "sf_cd321x_events.h"
#include "sf_cd321x_mask.h"
#include <mach/mach_time.h>
#include <IOKit/IOService.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>

#define LOG(fmt, ...) IOLog("SFAppleTypeC: " fmt "\n", ##__VA_ARGS__)
static const char *const hpmPath = "/arm-io/i2c0/hpmBusManager/hpm1";

class SFAppleTypeC : public IOService {
    OSDeclareDefaultStructors(SFAppleTypeC)
    SFI2C bus;
    IOWorkLoop *loop;
    IOTimerEventSource *timer;
    IOCommandGate *gate;
    uint32_t acceptingControls,controlCount;
    uint64_t eventReads,eventAcks,eventErrors;
    uint8_t address;
    uint32_t lastStatus, lastData, lastVdo, lastFlags, samples, failures;
    bool valid;
    int blockRead(uint8_t reg, void *dst, unsigned len);
    int blockWrite(uint8_t reg,const void *src,unsigned len);
    static int powerRead(void *p,uint8_t reg,void *dst,size_t len) { return ((SFAppleTypeC *)p)->blockRead(reg,dst,(unsigned)len); }
    static int powerWrite(void *p,uint8_t reg,const void *src,size_t len) { return ((SFAppleTypeC *)p)->blockWrite(reg,src,(unsigned)len); }
    static uint64_t powerNow(void *) { uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000000; }
    static void powerSleep(void *,unsigned ms) { IOSleep(ms); }
    void sample();
    int readControl();
    static IOReturn controlAction(OSObject *,void *,void *,void *,void *);
    static void tick(OSObject *owner, IOTimerEventSource *source);
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    IOReturn callPlatformFunction(const OSSymbol *,bool,void *,void *,void *,void *) override;
};

#define super IOService
OSDefineMetaClassAndStructors(SFAppleTypeC, IOService)
static volatile UInt32 claimed;

IOService *SFAppleTypeC::probe(IOService *provider, SInt32 *score)
{
    int enabled = 0;
    uint32_t chip = 0;
    if (!PE_parse_boot_argn("sftypec", &enabled, sizeof(enabled)) || enabled != 1)
        return NULL;
    if (!sf_adt_u32("/chosen", "chip-id", 0, &chip) || chip != 0x8103)
        return NULL;
    // The installed boot chain removes the bench's port-0 nodes. Require the
    // expected surviving CD3217 node/address, rather than probing other buses.
    uint32_t addr = 0;
    if (!sf_adt_u32(hpmPath, "hpm-iic-addr", 0, &addr) || addr != 0x3f)
        return NULL;
    IORegistryEntry *root = IORegistryEntry::fromPath("/", gIODTPlane);
    bool board = root && IODTMatchNubWithKeys(root, "J293AP");
    OSSafeReleaseNULL(root);
    if (!board || !OSCompareAndSwap(0, 1, &claimed))
        return NULL;
    address = (uint8_t)addr;
    return super::probe(provider, score);
}

int SFAppleTypeC::blockRead(uint8_t reg, void *dst, unsigned len)
{
    uint8_t wire[SF_TPS_MAX_LEN + 1];
    if (!len || len > SF_TPS_MAX_LEN)
        return EINVAL;
    SFI2CMsg msg[2] = {{address, false, &reg, 1}, {address, true, wire, len + 1}};
    int ret = bus.transfer(msg, 2);
    return ret ? ret : sf_cd321x_unpack(wire, len + 1, dst, len);
}

int SFAppleTypeC::blockWrite(uint8_t reg,const void *src,unsigned len)
{
    if(!src || !len || len>SF_TPS_MAX_LEN)return EINVAL;
    uint8_t wire[SF_TPS_MAX_LEN+2];wire[0]=reg;wire[1]=(uint8_t)len;
    memcpy(wire+2,src,len);
    SFI2CMsg msg={address,false,wire,len+2};return bus.transfer(&msg,1);
}

void SFAppleTypeC::sample()
{
    uint8_t status[4], data[4], intel[9];
    // Same ordering as Linux cd321x_interrupt: acknowledge the exact
    // observed EVENT1 word before re-reading negotiated status. This shares
    // the existing slow sampler, not an unhandled GPIO interrupt or new poll.
    sf_cd321x_events_result events={};
    int ret=sf_cd321x_events_ack(this,powerRead,powerWrite,&events);
    if(events.read_valid)++eventReads;
    if(events.acknowledged)++eventAcks;
    if(ret)++eventErrors;
    setProperty("event-read-count",eventReads,64);
    setProperty("event-ack-count",eventAcks,64);
    setProperty("event-error-count",eventErrors,64);
    setProperty("event-last-error",(uint32_t)ret,32);
    setProperty("event-ack-uncertain",events.acknowledge_attempted && !events.acknowledged);
    if(events.read_valid)setProperty("event-last-pending",events.pending,sizeof(events.pending));
    if (!ret) ret = blockRead(SF_TPS_REG_STATUS, status, sizeof(status));
    if (!ret)
        ret = blockRead(SF_TPS_REG_DATA_STATUS, data, sizeof(data));
    uint32_t vdo=0, flags=0;
    if (!ret && (sf_cd321x_le32(data) & SF_TPS_DATA_TBT)) {
        ret = blockRead(SF_TPS_REG_INTEL_VID_STATUS, intel, sizeof(intel));
        if (!ret) { vdo=sf_cd321x_tbt_vdo(intel); flags=SF_CD321X_TBT_VALID; }
    }
    samples++;
    setProperty("sample-count", samples, 32);
    setProperty("last-error", (uint32_t)ret, 32);
    setProperty("sample-valid", ret == 0);
    if (ret) {
        failures++;
        setProperty("error-count", failures, 32);
        if (failures == 1 || failures % 60 == 0)
            LOG("port 1 status read failed errno=%d (failed samples=%u)", ret, failures);
        struct sf_cd321x_cable_snapshot snapshot = {{lastStatus, lastData, samples, (uint32_t)ret},lastVdo,lastFlags};
        setProperty("port-state", &snapshot, sizeof(snapshot));
        messageClients(kIOMessageServicePropertyChange);
        // Failed samples never become disconnects or usable role changes.
        return;
    }
    uint32_t s = sf_cd321x_le32(status), d = sf_cd321x_le32(data);
    setProperty("status", s, 32);
    setProperty("data-status", d, 32);
    setProperty("plug-present", (bool)(s & SF_TPS_STATUS_PLUG_PRESENT));
    setProperty("orientation-reversed", (bool)(s & SF_TPS_STATUS_UPSIDE_DOWN));
    setProperty("pd-data-role-host", (bool)(s & SF_TPS_STATUS_DATAROLE));
    enum sf_cd321x_usb_role role = sf_cd321x_usb_role(s, d);
    setProperty("usb-role", role == SF_CD321X_USB_HOST ? "host" :
        role == SF_CD321X_USB_DEVICE ? "device" : "none");
    setProperty("usb2", (bool)(d & SF_TPS_DATA_USB2));
    setProperty("usb3", (bool)(d & SF_TPS_DATA_USB3));
    setProperty("displayport", (bool)(d & SF_TPS_DATA_DP));
    setProperty("thunderbolt", (bool)(d & SF_TPS_DATA_TBT));
    setProperty("usb4", (bool)(d & SF_CD321X_DATA_USB4));
    setProperty("hpd", (bool)(d & SF_CD321X_DATA_HPD));
    if (!valid || s != lastStatus || d != lastData)
        LOG("port 1 status=%08x data=%08x present=%u reverse=%u sf_host=%u usb2=%u usb3=%u dp=%u tbt=%u usb4=%u hpd=%u (observed, PHY/host not started)",
            s, d, !!(s & SF_TPS_STATUS_PLUG_PRESENT), !!(s & SF_TPS_STATUS_UPSIDE_DOWN),
            !!(s & SF_TPS_STATUS_DATAROLE), !!(d & SF_TPS_DATA_USB2), !!(d & SF_TPS_DATA_USB3),
            !!(d & SF_TPS_DATA_DP), !!(d & SF_TPS_DATA_TBT), !!(d & SF_CD321X_DATA_USB4),
            !!(d & SF_CD321X_DATA_HPD));
    lastStatus = s;
    lastData = d;
    lastVdo = vdo;
    lastFlags = flags;
    valid = true;
    struct sf_cd321x_cable_snapshot snapshot = {{s, d, samples, 0},vdo,flags};
    setProperty("port-state", &snapshot, sizeof(snapshot));
    messageClients(kIOMessageServicePropertyChange);
}

void SFAppleTypeC::tick(OSObject *owner, IOTimerEventSource *source)
{
    SFAppleTypeC *self = OSDynamicCast(SFAppleTypeC, owner);
    if (!self) return;
    self->sample();
    source->setTimeoutMS(1000);
}
int SFAppleTypeC::readControl()
{
    if(controlCount==UINT32_MAX)return EOVERFLOW;
    sf_cd321x_control value={};
    int ret=sf_cd321x_control_observe(this,powerRead,&value);
    setProperty("control-read-count",++controlCount,32);
    setProperty("control-read-error",(uint32_t)ret,32);
    setProperty("control-read-valid",ret==0);
    if(!ret){
        bool ok=setProperty("control-event1",value.event1,sizeof(value.event1)) &&
            setProperty("control-mask1",value.mask1,sizeof(value.mask1)) &&
            setProperty("control-power",(uint32_t)value.power,32) &&
            setProperty("control-status",sf_cd321x_le32(value.status),32) &&
            setProperty("control-data-status",sf_cd321x_le32(value.data),32);
        if(!ok){ret=ENOMEM;setProperty("control-read-error",(uint32_t)ret,32);setProperty("control-read-valid",false);}
    }
    return ret;
}
IOReturn SFAppleTypeC::controlAction(OSObject *owner,void *,void *,void *,void *)
{
    auto self=OSDynamicCast(SFAppleTypeC,owner);
    if(!self || !__atomic_load_n(&self->acceptingControls,__ATOMIC_ACQUIRE))return kIOReturnNotReady;
    return self->readControl()?kIOReturnIOError:kIOReturnSuccess;
}
IOReturn SFAppleTypeC::callPlatformFunction(const OSSymbol *name,bool wait,void *a,void *b,void *c,void *d)
{
    if(name && name->isEqualTo("SFAppleTypeCControlSnapshot")){
        if(a || b || c || d)return kIOReturnBadArgument;
        if(!gate || !__atomic_load_n(&acceptingControls,__ATOMIC_ACQUIRE))return kIOReturnNotReady;
        return gate->runAction(controlAction);
    }
    return IOService::callPlatformFunction(name,wait,a,b,c,d);
}

bool SFAppleTypeC::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    setProperty("port-index", 1, 32);
    setProperty("port-label", "USB-C Left-front");
    setProperty("observer-only", false);
    setProperty("setup-policy","port1-S0-Linux-mask-polled");
    setProperty("event-policy","polled-1Hz-exact-EVENT1-CLEAR1-mask-unchanged");
    setProperty("hpm-path", hpmPath);
    setProperty("error-count", (uint32_t)0, 32);
    if (!bus.attach("/arm-io/i2c0", 100000)) {
        LOG("I2C0 attach failed");
        return false;
    }
    uint8_t vid[4], mode[4];
    int ret = blockRead(SF_TPS_REG_VID, vid, sizeof(vid));
    if (!ret) ret = blockRead(SF_TPS_REG_MODE, mode, sizeof(mode));
    if (ret || !sf_cd321x_le32(vid)) {
        LOG("CD321x identity/mode read failed errno=%d", ret ? ret : ENODEV);
        return false;
    }
    setProperty("vendor-id", sf_cd321x_le32(vid), 32);
    setProperty("firmware-mode", mode, sizeof(mode));
    if(memcmp(mode,"APP ",4)) { LOG("port1 not APP; no SSPS or PHY admission");return false; }
    sf_cd321x_power_io io={this,powerRead,powerWrite,powerNow,powerSleep};
    sf_cd321x_power_result power={};ret=sf_cd321x_ensure_s0(&io,&power);
    setProperty("system-power-before",(uint32_t)power.before,32);
    setProperty("system-power-after",(uint32_t)power.after,32);
    setProperty("system-power-command-issued",power.command_issued);
    setProperty("system-power-polls",(uint32_t)power.polls,32);
    setProperty("system-power-error",(uint32_t)ret,32);
    LOG("port1 S0 before=%u after=%u SSPS=%u polls=%u error=%d; port0 reserved",
        power.before,power.after,power.command_issued,power.polls,ret);
    if(ret)return false; // No state publication, PHY or ACIO start after failed setup.
    sf_cd321x_mask_result mask={};ret=sf_cd321x_ensure_mask(this,powerRead,powerWrite,&mask);
    setProperty("interrupt-mask-error",(uint32_t)ret,32);
    setProperty("interrupt-mask-write-attempted",mask.write_attempted);
    setProperty("interrupt-mask-before-valid",mask.before_valid);
    setProperty("interrupt-mask-after-valid",mask.after_valid);
    if(mask.before_valid)setProperty("interrupt-mask-before",mask.before,sizeof(mask.before));
    if(mask.after_valid)setProperty("interrupt-mask-after",mask.after,sizeof(mask.after));
    setProperty("interrupt-delivery","existing-1Hz-poll-no-GPIO-enable");
    if(ret){LOG("port1 Linux mask setup failed errno=%d; no PHY admission",ret);return false;}
    loop = IOWorkLoop::workLoop();
    timer = IOTimerEventSource::timerEventSource(this, tick);
    gate=IOCommandGate::commandGate(this);
    if (!loop || !timer || !gate || loop->addEventSource(timer) != kIOReturnSuccess ||
        loop->addEventSource(gate)!=kIOReturnSuccess)
        return false;
    controlCount=0;
    __atomic_store_n(&acceptingControls,1,__ATOMIC_RELEASE);
    readControl(); // Diagnostic failure never fabricates usable cable state.
    timer->setTimeoutMS(1);
    registerService();
    return true;
}

void SFAppleTypeC::stop(IOService *provider)
{
    __atomic_store_n(&acceptingControls,0,__ATOMIC_RELEASE);
    if (timer) {
        timer->cancelTimeout();
        timer->disable();
        // removeEventSource serializes with the workloop's current callback.
        if (loop) loop->removeEventSource(timer);
    }
    if(gate){gate->disable();if(loop)loop->removeEventSource(gate);}
    super::stop(provider);
}

void SFAppleTypeC::free()
{
    OSSafeReleaseNULL(timer);
    OSSafeReleaseNULL(gate);
    OSSafeReleaseNULL(loop);
    bus.regs.unmap();
    if (bus.lock) { IOLockFree(bus.lock); bus.lock = NULL; }
    super::free();
}
