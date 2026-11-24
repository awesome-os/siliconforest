/* SPDX-License-Identifier: MIT */
/*
 * SFM1SPIHID tells the Touch Bar driver about the Fn key and about typing. Included at the
 * end of SFM1SPIHID.cpp by ../build-spihid.nix (spihid-touchbar.patch); everything here
 * runs on the transport workloop.
 *
 * The Fn key is not a keyboard-page usage: the keyboard's own report descriptor declares
 * it as usage 0x03 of page 0xff in the keyboard report (ID 1), and Linux hid-apple takes
 * that usage as KEY_FN. Its bit is looked up in the descriptor the keyboard handed over,
 * not assumed.
 */
#include "sf_hid_fn.h"
#include "sf_touchbar_kbd.h"

void SFM1SPIHID::tbConfigure()
{
    unsigned width=0;
    tbFnBit=sf_hid_input_field(descriptor,descriptorLength,1,0xff,0x03,&width);
    tbFnBits=(uint8_t)width;
    /* the descriptor itself, once: it is the keyboard model's, not this machine's */
    for (size_t at=0; at<descriptorLength; at+=32) {
        char line[3*32+1]; size_t used=0;
        for (size_t i=at; i<descriptorLength && i<at+32; i++) used+=snprintf(line+used,sizeof(line)-used,"%02x ",descriptor[i]);
        IOLog("SFM1SPIHID: keyboard descriptor +%u: %s\n",(unsigned)at,line);
    }
    tbFnDown=0; tbNotes=0; tbLastNoteUS=0; tbService=nullptr;
    if (tbFnBit>=0)
        IOLog("SFM1SPIHID: Fn key is %u bit(s) at bit %d of keyboard report 1 (usage page 0xff, usage 0x03 of the keyboard's descriptor)\n",(unsigned)tbFnBits,tbFnBit);
    else
        IOLog("SFM1SPIHID: the keyboard's descriptor (%u bytes) has no Fn usage in report 1; the Touch Bar is told about typing only\n",
              (unsigned)descriptorLength);
}

/* One keyboard input report (ID 1), real or made up by a test. */
void SFM1SPIHID::tbKeyboardReport(const uint8_t *report, size_t length)
{
    uint8_t fn=0;
    /* any bit of the field set: down */
    for (unsigned b=0; tbFnBit>=0 && b<tbFnBits && (size_t)((tbFnBit+b)/8)<length; b++)
        fn|=(report[(tbFnBit+b)/8]>>((tbFnBit+b)%8))&1;
    uint64_t now=SFM1KeyboardHW::clockUS();
    /* every change of Fn; typing at most four times a second */
    if (fn==tbFnDown && tbLastNoteUS && now-tbLastNoteUS<250000) return;
    if (!tbService) {
        /* the Touch Bar's driver may start after the keyboard's, or not be there at all */
        OSDictionary *match=IOService::serviceMatching(kSFTouchBarServiceClass);
        tbService=match ? IOService::copyMatchingService(match) : nullptr;
        if (match) match->release();
    }
    tbLastNoteUS=now;
    if (!tbService) return;
    if (fn!=tbFnDown && tbNotes<8)
        IOLog("SFM1SPIHID: Fn %s; told to the Touch Bar\n",fn ? "down" : "up");
    tbFnDown=fn; tbNotes++;
    tbService->message(kSFTouchBarKeyboardMessage,this,(void *)(uintptr_t)
        ((fn ? kSFTouchBarKeyboardFn : 0)|kSFTouchBarKeyboardActivity|(tbFnBit>=0 ? kSFTouchBarKeyboardFnKnown : 0)));
}

IOReturn SFM1SPIHID::tbTestGated(OSObject *owner, void *fn, void *, void *, void *)
{
    SFM1SPIHID *self=OSDynamicCast(SFM1SPIHID,owner);
    uint8_t report[32];
    if (!self || self->stopping || !self->published) return kIOReturnNotReady;
    /* without a known Fn bit a report with Fn up can still be made up: it is typing */
    if (fn && (self->tbFnBit<0 || (size_t)(self->tbFnBit/8)>=sizeof(report))) return kIOReturnUnsupported;
    bzero(report,sizeof(report));
    report[0]=0x01;
    if (fn) report[self->tbFnBit/8]|=(uint8_t)(1u<<(self->tbFnBit%8));
    size_t length=self->tbFnBit<0 ? 9 : (size_t)(self->tbFnBit/8)+1;
    self->tbLastNoteUS=0;       /* a test report is never held back by the typing rate */
    self->tbKeyboardReport(report,length<9 ? 9 : length);
    return kIOReturnSuccess;
}

IOReturn SFM1SPIHID::message(UInt32 type, IOService *provider, void *argument)
{
    if (type==kSFTouchBarTestKeyboardReport)
        return transportGate ? transportGate->runAction(tbTestGated,(void *)((uintptr_t)argument&1)) : kIOReturnNotReady;
    return super::message(type,provider,argument);
}
