// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SFM1SPIHID multitouch mode: request touch data from the SPI trackpad the way
 * Omacom linux-aurora does (integration/aurora 6a08a08e), decode it
 * (m1tp_mt.c) and deliver synthesized pointer reports through the child HID
 * device. Included by SFM1SPIHID.cpp; everything runs on the transport
 * workloop.
 *
 * Mode request ported from magicmouse_enable_multitouch() in
 * drivers/hid/hid-magicmouse.c
 *   Copyright (c) 2010 Michael Poole <mdpoole@troilus.org>
 *   Copyright (c) 2010 Chase Douglas <chase.douglas@canonical.com>
 * and apple_ll_raw_request() in drivers/hid/spi-hid/spi-hid-apple-core.c
 *   Copyright (C) The Asahi Linux Contributors
 *   Copyright (c) 2015-2018 Federico Lorenzi
 *   Copyright (c) 2017-2018 Ronald Tschalär
 */

/* Linux re-sends the request whenever the input device is opened and after
 * every reset report; here a bound keeps a misbehaving controller from being
 * written to for ever. */
#define SF_M1_MT_MAX_MODE_REQUESTS 8u

#include <sys/errno.h>
#include <sys/sysctl.h>
#include <IOKit/IOLocks.h>

/* ---- Three- and four-finger swipes for the desktop (night of 14 November) ----------------
 * The pointer record that reaches /dev/usb_hid_mouse has no room for a swipe (buttons, motion,
 * one wheel byte), and IOHIDFamily's queue is left as it is. A swipe the decoder recognises
 * (m1tp_mt_out.swipe) goes into a small ring here instead, which the compositor reads once a
 * frame through `sysctl kern.sf_trackpad_swipe` (opaque, read-only: a version, the next
 * sequence number and the last 32 events). `sysctl -w kern.sf_trackpad_swipe_test=<n>` (root)
 * sends a constructed swipe through the same decoder on a scratch state, report by report in
 * real time (8 ms apart): n = 3 or 4 fingers to the left, -3 or -4 to the right; those events
 * are marked synthetic. The sysctls are registered once and never removed (the kext is part of
 * the collection and is not unloaded; the handlers touch only these statics). */
#include "sf_tp_gesture_ring.h"
static struct sf_tp_swipe_ring gPdTpSwipe = { 2, 1, {} };
static IOLock *gPdTpSwipeLock;
static struct m1tp_mt_state gPdTpSwipeScratch;
static struct m1tp_mt_frame gPdTpSwipeFrame;
static uint8_t gPdTpSwipeReport[M1TP_MT_MAX_REPORT_BYTES];
static uint32_t gPdTpSwipeLogged;

static void sf_tp_swipe_push(const struct m1tp_mt_out *out, bool synthetic)
{
    if (!out->swipe || !gPdTpSwipeLock) return;
    IOLockLock(gPdTpSwipeLock);
    struct sf_tp_swipe_event *e=&gPdTpSwipe.ev[gPdTpSwipe.next_seq % SF_TP_SWIPE_RING];
    e->seq=gPdTpSwipe.next_seq++;
    e->phase=out->swipe; e->fingers=out->swipe_fingers; e->dx=out->swipe_dx; e->dy=out->swipe_dy;
    e->synthetic=synthetic?1:0; e->kind=out->gesture_kind;
    e->scale_q16=out->gesture_kind?out->scale_q16:65536; e->angle_cdeg=out->angle_cdeg;
    e->ms=(uint32_t)(SFM1KeyboardHW::clockUS()/1000);
    IOLockUnlock(gPdTpSwipeLock);
    if (out->swipe==M1TP_MT_SWIPE_BEGIN && gPdTpSwipeLogged<4) {
        gPdTpSwipeLogged++;
        IOLog("SFM1SPIHID: MT stage=gesture-begin fingers=%u kind=%u synthetic=%u (kern.sf_trackpad_swipe)\n",
              (unsigned)out->swipe_fingers,(unsigned)out->gesture_kind,synthetic?1u:0u);
    }
}

static int sf_tp_swipe_read SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
    struct sf_tp_swipe_ring copy;
    if (!gPdTpSwipeLock) return ENOENT;
    IOLockLock(gPdTpSwipeLock);
    copy=gPdTpSwipe;
    IOLockUnlock(gPdTpSwipeLock);
    return SYSCTL_OUT(req,&copy,sizeof(copy));
}

/* A swipe of 40 mm in 41 reports, 8 ms apart, through m1tp_mt_parse() and m1tp_mt_step() on a
 * scratch state, then the fingers lifted one per report. */
static int sf_tp_swipe_test SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
    int value=0, error=SYSCTL_IN(req,&value,sizeof(value));
    if (error || !req->newptr) return error ? error : SYSCTL_OUT(req,&value,sizeof(value));
    if (value==2 || value==-2 || value==90) {
        if (!gPdTpSwipeLock) return EINVAL;
        struct sf_tp_pinch_scratch {
            struct m1tp_mt_state state;
            struct m1tp_mt_frame frame;
            struct m1tp_mt_out out;
            uint8_t report[M1TP_MT_MAX_REPORT_BYTES];
        };
        auto memory=(sf_tp_pinch_scratch *)IOMallocZero(sizeof(sf_tp_pinch_scratch));
        if (!memory) return ENOMEM;
        auto &scratch=memory->state;auto &frame=memory->frame;auto &out=memory->out;
        auto &report=memory->report;int result=0;
        static const int16_t quarter[11][2]={{1000,0},{988,156},{951,309},{891,454},
            {809,588},{707,707},{588,809},{454,891},{309,951},{156,988},{0,1000}};
        m1tp_mt_init(&scratch,nullptr);
        for (int i=0;i<=44;++i) {
            int16_t xy[2][2];
            int n=i<=40?2:0, step=i<=40?i:40;
            int x=value==-2?2600-step*40:1000+step*40, y=0;
            if (value==90) {x=quarter[step/4][0]*2;y=quarter[step/4][1]*2;}
            xy[0][0]=(int16_t)-x;xy[1][0]=(int16_t)x;
            xy[0][1]=(int16_t)(3000+y);xy[1][1]=(int16_t)(3000-y);
            uint64_t t=SFM1KeyboardHW::clockUS();
            size_t size=m1tp_mt_build(report,sizeof(report),0,(uint16_t)(t/1000),(unsigned)n,
                                      (const int16_t (*)[2])xy,nullptr);
            if (!size || m1tp_mt_parse(report,size,&frame)!=M1TP_MT_KIND_TOUCH) {result=EIO;break;}
            (void)m1tp_mt_step(&scratch,&frame,t,&out);
            sf_tp_swipe_push(&out,true);IOSleep(8);
        }
        if (!result) IOLog("SFM1SPIHID: MT stage=pinch-test value=%d (synthetic scratch reports)\n",value);
        IOFree(memory,sizeof(*memory));return result;
    }
    if (value!=3 && value!=-3 && value!=4 && value!=-4) return EINVAL;
    int fingers=value<0?-value:value, sign=value<0?1:-1;
    if (fingers<3 || fingers>4 || !gPdTpSwipeLock) return EINVAL;
    int16_t xy[4][2];
    struct m1tp_mt_out out;
    uint64_t t=SFM1KeyboardHW::clockUS();
    m1tp_mt_init(&gPdTpSwipeScratch,nullptr);
    for (int i=0;i<=40+fingers;i++) {
        int n=i<=40?fingers:fingers-(i-40);
        int step=i<=40?i:40;
        for (int f=0;f<fingers;f++) {
            xy[f][0]=(int16_t)(sign<0?2000:-2000)+(int16_t)(f*1500)+(int16_t)(sign*step*98);
            xy[f][1]=3000;
        }
        size_t size=m1tp_mt_build(gPdTpSwipeReport,sizeof(gPdTpSwipeReport),0,(uint16_t)(t/1000),(unsigned)n,
                                  (const int16_t (*)[2])xy,nullptr);
        if (!size || m1tp_mt_parse(gPdTpSwipeReport,size,&gPdTpSwipeFrame)!=M1TP_MT_KIND_TOUCH) return EIO;
        (void)m1tp_mt_step(&gPdTpSwipeScratch,&gPdTpSwipeFrame,t,&out);
        sf_tp_swipe_push(&out,true);
        IOSleep(8);
        t=SFM1KeyboardHW::clockUS();
    }
    IOLog("SFM1SPIHID: MT stage=swipe-test fingers=%d direction=%s next_seq=%u (synthetic reports through the decoder)\n",
          fingers,sign<0?"left":"right",(unsigned)gPdTpSwipe.next_seq);
    return 0;
}

SYSCTL_DECL(_kern);
SYSCTL_PROC(_kern, OID_AUTO, sf_trackpad_swipe, CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_LOCKED, 0, 0,
            sf_tp_swipe_read, "S", "version2 trackpad gesture ring: swipe/pinch, next sequence, last32 events");
SYSCTL_PROC(_kern, OID_AUTO, sf_trackpad_swipe_test, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, 0, 0,
            sf_tp_swipe_test, "I", "synthetic scratch reports: +/-3,4 swipe; +/-2 pinch out/in;90 clockwise turn");

static void sf_tp_swipe_register()
{
    if (gPdTpSwipeLock) return;
    gPdTpSwipeLock=IOLockAlloc();
    if (!gPdTpSwipeLock) return;
    sysctl_register_oid(&sysctl__kern_sf_trackpad_swipe);
    sysctl_register_oid(&sysctl__kern_sf_trackpad_swipe_test);
}

/* Boot-args (all optional; the trackpad child needs sfm1trackpadchild=1 as before).
 * The defaults reproduce this M1 under Omarchy Linux: libinput 1.32.0 with Hyprland's
 * settings there; where each number comes from is in m1tp_mt.h and m1tp_mt.c.
 *   sfm1tpmt=0            basic pointer mode, the behaviour before multitouch
 *   sfm1tpgain=N          pointer counts per libinput unit, percent (default 200: Linux runs
 *                         the panel at scale 2, a count here is one panel pixel)
 *   sfm1tpaccel=N         speed in mm/s where acceleration starts (default 130)
 *   sfm1tpscale=N         no acceleration curve: N contact units per count (98 units = 1 mm)
 *   sfm1tpscrollfactor=N  scroll speed in 1/1000 (default 400, Hyprland scroll_factor 0.4)
 *   sfm1tpscroll=N        instead: N contact units of two-finger travel per wheel step
 *                         (the defaults give 346 = 3.57 mm)
 *   sfm1tpnatural=0       wheel direction of a mouse wheel (default 1: content follows fingers)
 *   sfm1tpdwt=0           no disable-while-typing
 *   sfm1tpthumb=0         no thumb detection
 *   sfm1tppalm=N          touch size above which a touch is a palm (default 1600)
 *   sfm1tptouchlow=N, sfm1tptouchhigh=N   touch size hysteresis (default 130, 150)
 *   sfm1tpresx=N, sfm1tpresy=N            contact units per mm (default 98, 97)
 *   sfm1tpheader=1        take pointer motion from the touch report's own pointer header
 *   sfm1tpdebug=N         print the first N decoded reports (MT_DBG lines)
 *   sfm1tpselftest=1      self-test at start (MT_SELFTEST and MT_MEASURED lines)
 *   sfm1tpselftest=2      also deliver the self-test's first scenario through the child HID
 *                         device when /dev/usb_hid_kbd is first opened (MT_SELFTEST_INJECT
 *                         line); for mt-selftest-prelude.in only
 * Tapping is not implemented: it is off on the reference desktop (tap_to_click = false). */
void SFM1SPIHID::mtConfigure()
{
    int on=1, value=0;
    if (PE_parse_boot_argn("sfm1tpmt",&on,sizeof(on)) && on!=1) on=0;
    mtEnabled=t2Enabled && on==1;
    bzero(&mtConfig,sizeof(mtConfig));      /* zero: m1tp_mt_init() takes the default */
    if (PE_parse_boot_argn("sfm1tpgain",&value,sizeof(value)) && value>0) mtConfig.gain_percent=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tpaccel",&value,sizeof(value)) && value>0) mtConfig.accel_threshold=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tpscale",&value,sizeof(value)) && value>0) mtConfig.flat_units_per_count=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tpscrollfactor",&value,sizeof(value)) && value>0) mtConfig.scroll_factor_permille=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tpscroll",&value,sizeof(value)) && value>0) mtConfig.scroll_units_per_step=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tpnatural",&value,sizeof(value))) mtConfig.natural_scroll=value==1?1:M1TP_MT_OFF;
    if (PE_parse_boot_argn("sfm1tpdwt",&value,sizeof(value))) mtConfig.dwt=value==1?1:M1TP_MT_OFF;
    if (PE_parse_boot_argn("sfm1tpthumb",&value,sizeof(value))) mtConfig.thumb=value==1?1:M1TP_MT_OFF;
    if (PE_parse_boot_argn("sfm1tppalm",&value,sizeof(value)) && value>0) mtConfig.palm_size=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tptouchlow",&value,sizeof(value)) && value>0) mtConfig.touch_low=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tptouchhigh",&value,sizeof(value)) && value>0) mtConfig.touch_high=(uint32_t)value;
    if (PE_parse_boot_argn("sfm1tpresx",&value,sizeof(value)) && value>0) mtConfig.res_x=value;
    if (PE_parse_boot_argn("sfm1tpresy",&value,sizeof(value)) && value>0) mtConfig.res_y=value;
    if (PE_parse_boot_argn("sfm1tpheader",&value,sizeof(value)) && value==1) mtConfig.use_header_motion=1;
    if (PE_parse_boot_argn("sfm1tpdebug",&value,sizeof(value)) && value>0)
        mtDebug=(uint32_t)(value>1000?1000:value);
    mtDebugHex=mtDebug<4?mtDebug:4;
    if (PE_parse_boot_argn("sfm1tpselftest",&value,sizeof(value)) && (value==1 || value==2))
        mtSelftest=value;
    m1tp_mt_init(&mtState,&mtConfig);
    if (mtEnabled) sf_tp_swipe_register();
    const struct m1tp_mt_config *c=&mtState.cfg;
    IOLog("SFM1SPIHID: MT stage=config enabled=%u reference=libinput-1.32.0 res=%d/%d units_per_mm gain_percent=%u accel_from_mm_s=%u flat_units_per_count=%u scroll_factor_permille=%u scroll_units_per_step=%u natural=%u dwt=%u thumb=%u palm_size=%u touch_size=%u/%u header_motion=%u debug_reports=%u selftest=%d\n",
          mtEnabled?1u:0u,(int)c->res_x,(int)c->res_y,(unsigned)c->gain_percent,(unsigned)c->accel_threshold,
          (unsigned)c->flat_units_per_count,(unsigned)c->scroll_factor_permille,
          (unsigned)((((uint64_t)c->scroll_step_px)<<16)/(uint64_t)(mtState.scroll_per_unit>0?mtState.scroll_per_unit:1)),
          (unsigned)c->natural_scroll,(unsigned)c->dwt,(unsigned)c->thumb,(unsigned)c->palm_size,
          (unsigned)c->touch_low,(unsigned)c->touch_high,(unsigned)c->use_header_motion,(unsigned)mtDebug,mtSelftest);
}

void SFM1SPIHID::mtSendEnable()
{
    mtModeDueUS=0;
    if (mtModeAttempts>=SF_M1_MT_MAX_MODE_REQUESTS) return;
    /* spihid_apple_request(spihid, 2, 0x52, 0x02, 0x00, 2, {0x02, 0x01}, 2):
     * SET_REPORT of feature report 2 on the trackpad interface. */
    uint8_t id=nextID++;
    if (dlsp_request(2,M1TP_MT_ENABLE_COMMAND,M1TP_MT_ENABLE_REPORT,0x00,id,
                     M1TP_MT_ENABLE_RESPONSE_BYTES,m1tp_mt_enable_payload,
                     sizeof(m1tp_mt_enable_payload),packet)!=0) {
        IOLog("SFM1SPIHID: MT stage=mode-write not encoded; staying in basic mode\n");
        return;
    }
    int e=hardware.writePacket(packet);
    mtModeAttempts++;
    IOLog("SFM1SPIHID: MT stage=mode-write attempt=%u request_id=%u result=%d\n",
          (unsigned)mtModeAttempts,(unsigned)id,e);
    /* A failed write has already faulted the shared transport. */
    if (e) fail(e,"trackpad-mode-write");
}

bool SFM1SPIHID::mtSubmit(const struct m1tp_mt_out *out)
{
    uint8_t bytes[M1TP_MT_POINTER_REPORT_BYTES];
    SFM1SPITrackpad *child=childSink;
    if (!child) return false;
    if (!child->admitting() || child->isInactive()) { retireChild(child); return false; }
    m1tp_mt_encode(out,bytes);
    IOBufferMemoryDescriptor *report=IOBufferMemoryDescriptor::withBytes(bytes,sizeof(bytes),kIODirectionNone);
    if (!report) { childFault((int32_t)kIOReturnNoMemory); return false; }
    retain(); child->retain(); t2Dispatches++;
    /* Same synchronous delivery as the basic-mode route (routeTrackpad). */
    IOReturn kr=child->handleReport(report,kIOHIDReportTypeInput);
    t2Dispatches--;
    bool okay=kr==kIOReturnSuccess;
    if (okay) {
        childSubmitted++; mtPointerReports++;
        if (mtPointerReports==1)
            IOLog("SFM1SPIHID: MT stage=first-pointer-report buttons=%02x dx=%d dy=%d wheel=%d accepted by the child HID device\n",
                  (unsigned)out->buttons,(int)out->dx,(int)out->dy,(int)out->wheel);
    } else childFault((int32_t)kr);
    report->release(); child->release(); release();
    return okay;
}

void SFM1SPIHID::mtRoute(const struct dlsp_message *message)
{
    struct m1tp_mt_out out;
    childObserved++; countersDirty=true;
    int kind=m1tp_mt_parse(message->payload,message->payload_length,&mtFrame);
    if (kind==M1TP_MT_KIND_INVALID) {
        /* Vendor reports 0x3f/0x44 and anything malformed: not pointer data. */
        childRejected++;
        if (childRejected==1)
            IOLog("SFM1SPIHID: MT stage=report-ignored id=%02x bytes=%u (first; counted in SFM1TrackpadRejected)\n",
                  message->payload_length?(unsigned)message->payload[0]:0u,(unsigned)message->payload_length);
        return;
    }
    if (kind==M1TP_MT_KIND_RESET) {
        (void)m1tp_mt_step(&mtState,&mtFrame,SFM1KeyboardHW::clockUS(),&out);
        sf_tp_swipe_push(&out,false);
        /* magicmouse_raw_event_spi(): "Touch controller was reset,
         * re-enabling touch mode", 10 ms later. */
        mtResets++;
        if (mtModeAttempts<SF_M1_MT_MAX_MODE_REQUESTS) mtModeDueUS=SFM1KeyboardHW::clockUS()+10000;
        if (mtResets<=4)
            IOLog("SFM1SPIHID: MT stage=reset-report count=%u mode_requests=%u (requesting touch data again)\n",
                  (unsigned)mtResets,(unsigned)mtModeAttempts);
        return;
    }
    if (kind==M1TP_MT_KIND_TOUCH) {
        if (!mtTouchReports++)
            IOLog("SFM1SPIHID: MT stage=first-touch-report bytes=%u records=%u contacts=%u (controller is in multitouch mode)\n",
                  (unsigned)message->payload_length,(unsigned)mtFrame.reported,(unsigned)mtFrame.contacts);
    } else if (!mtBasicReports++ && mtModeAttempts) {
        IOLog("SFM1SPIHID: MT stage=basic-report-after-mode-write bytes=%u (passed through as before)\n",
              (unsigned)message->payload_length);
    }
    int emit=m1tp_mt_step(&mtState,&mtFrame,SFM1KeyboardHW::clockUS(),&out);
    uint32_t fresh=mtState.events & ~mtLoggedEvents;
    if (fresh) {
        mtLoggedEvents|=fresh;
        if (fresh & M1TP_MT_EV_HEADER_MOTION)
            IOLog("SFM1SPIHID: MT stage=pointer-header-motion dx=%d dy=%d in a touch report (used for the pointer: %u; sfm1tpheader=1 selects it)\n",
                  (int)mtFrame.mouse_dx,(int)mtFrame.mouse_dy,(unsigned)mtState.cfg.use_header_motion);
        if (fresh & M1TP_MT_EV_RIGHT_CLICK)
            IOLog("SFM1SPIHID: MT stage=first-two-finger-click reported as the right button\n");
        if (fresh & M1TP_MT_EV_SCROLL)
            IOLog("SFM1SPIHID: MT stage=first-two-finger-scroll wheel=%d\n",(int)out.wheel);
        if (fresh & M1TP_MT_EV_THUMB)
            IOLog("SFM1SPIHID: MT stage=first-thumb state=%u (a resting thumb neither moves the pointer nor counts for a click)\n",
                  (unsigned)mtState.thumb_state);
        if (fresh & M1TP_MT_EV_PALM)
            IOLog("SFM1SPIHID: MT stage=first-palm typing=%u (touch ignored: palm-sized, or landed while typing)\n",
                  (unsigned)mtState.keyboard_active);
        if (fresh & M1TP_MT_EV_DEVICE_CLOCK)
            IOLog("SFM1SPIHID: MT stage=device-clock report interval taken from the controller's millisecond counter\n");
    }
    if (mtDebug) mtDump(message,emit,&out);
    sf_tp_swipe_push(&out,false);
    if (emit) (void)mtSubmit(&out);
}

/* sfm1tpdebug=N: the first N reports that carry a contact, a button or a
 * result, decoded; the first four of them also as bytes. */
void SFM1SPIHID::mtDump(const struct dlsp_message *message, int emit, const struct m1tp_mt_out *out)
{
    static const char digits[]="0123456789abcdef";
    const struct m1tp_mt_frame *f=&mtFrame;
    if (!emit && !f->contacts && !f->reported && !f->tp_buttons && !f->mouse_buttons &&
        !f->mouse_dx && !f->mouse_dy && mtTouchReports+mtBasicReports>2) return;
    mtDebug--;
    IOLog("SFM1SPIHID: MT_DBG kind=%u bytes=%u ts_ms=%u header=%02x/%d/%d tp_buttons=%02x records=%u contacts=%u down=%u gesture=%u thumb=%u typing=%u speed_um_s=%lld factor_e4=%d emit=%d out=%02x/%d/%d/%d\n",
          (unsigned)f->kind,(unsigned)message->payload_length,(unsigned)f->timestamp_ms,(unsigned)f->mouse_buttons,
          (int)f->mouse_dx,(int)f->mouse_dy,(unsigned)f->tp_buttons,(unsigned)f->reported,(unsigned)f->contacts,
          (unsigned)mtState.fingers_down,(unsigned)mtState.gesture,(unsigned)mtState.thumb_state,
          (unsigned)mtState.keyboard_active,(long long)mtState.last_velocity,
          (int)((int64_t)mtState.last_factor*10000/65536),emit,
          (unsigned)out->buttons,(int)out->dx,(int)out->dy,(int)out->wheel);
    for (unsigned i=0;i<f->contacts && i<4;i++)
        IOLog("SFM1SPIHID: MT_DBG contact=%u x=%d y=%d rel=%d/%d major=%d minor=%d pressure=%d\n",
              i,(int)f->c[i].x,(int)f->c[i].y,(int)f->c[i].rel_x,(int)f->c[i].rel_y,
              (int)f->c[i].touch_major,(int)f->c[i].touch_minor,(int)f->c[i].pressure);
    if (!mtDebugHex) return;
    mtDebugHex--;
    size_t total=message->payload_length<144?message->payload_length:144;
    for (size_t offset=0;offset<total;offset+=48) {
        char hex[97];
        size_t n=total-offset<48?total-offset:48;
        for (size_t i=0;i<n;i++) {
            hex[2*i]=digits[message->payload[offset+i]>>4];
            hex[2*i+1]=digits[message->payload[offset+i]&15];
        }
        hex[2*n]=0;
        IOLog("SFM1SPIHID: MT_DBG hex offset=%u data=%s\n",(unsigned)offset,hex);
    }
}

void SFM1SPIHID::mtInjectSink(void *context, const struct m1tp_mt_out *out)
{
    SFM1SPIHID *self=static_cast<SFM1SPIHID *>(context);
    self->mtInjectOffered++;
    if (self->mtSubmit(out)) self->mtInjectSubmitted++;
}

/* sfm1tpselftest: constructed multitouch reports through the decoder on a
 * scratch state (m1tp_mt_selftest.c). With inject, the first scenario's
 * reports are delivered through the child HID device, so they reach
 * /dev/usb_hid_mouse like a finger's would. */
void SFM1SPIHID::mtRunSelftest(bool inject)
{
    unsigned checks=0, reports=0;
    struct m1tp_mt_measured m;
    bzero(&m,sizeof(m));
    mtInjectSubmitted=0; mtInjectOffered=0;
    unsigned failed=m1tp_mt_selftest(&mtScratch,mtBuffer,sizeof(mtBuffer),&checks,&reports,&m,
                                     inject?mtInjectSink:nullptr,this);
    if (inject) {
        IOLog("SFM1SPIHID: MT_SELFTEST_INJECT %s pointer_reports=%u submitted=%u (first scenario through the child HID device)\n",
              !failed && mtInjectOffered && mtInjectSubmitted==mtInjectOffered?"PASS":"FAIL",
              (unsigned)mtInjectOffered,(unsigned)mtInjectSubmitted);
        return;
    }
    IOLog("SFM1SPIHID: MT_SELFTEST %s checks=%u pointer_reports=%u failed_check=%u failed_travel=%d (contact sequences in mm and ms through the decoder; totals equal the host-verified libinput 1.32.0 values)\n",
          failed?"FAIL":"PASS",checks,reports,failed,(int)m.failed_dx);
    /* Counts are panel pixels; libinput units are counts * 100 / gain. */
    IOLog("SFM1SPIHID: MT_MEASURED pointer counts: 10mm/s x 20mm = %d, 100mm/s x 40mm = %d, 400mm/s x 80mm = %d; recorded stroke (boot mt01) = %d/%d; scroll: 50mm two-finger travel = %d wheel steps\n",
          (int)m.slow_counts,(int)m.medium_counts,(int)m.fast_counts,(int)m.recorded_dx,(int)m.recorded_dy,
          (int)m.scroll_steps);
}
