/* SPDX-License-Identifier: MIT */
/*
 * What the built-in keyboard's driver (SFM1SPIHID) and the Touch Bar's (SFAppleTouchBar)
 * tell each other, as IOService messages: neither kext links against the other, each
 * finds the other's service by class name.
 */
#ifndef _SF_TOUCHBAR_KBD_H
#define _SF_TOUCHBAR_KBD_H

#include <IOKit/IOMessage.h>

/*
 * SFM1SPIHID -> SFAppleTouchBar, for a keyboard input report: the argument's bit 0 is the
 * state of the Fn key, bit 1 says the report is keyboard activity (always set), bit 2 that
 * the Fn key's place in the report is known.
 */
#define kSFTouchBarKeyboardMessage      iokit_vendor_specific_msg(0x5442)
#define kSFTouchBarKeyboardFn           0x1
#define kSFTouchBarKeyboardActivity     0x2
#define kSFTouchBarKeyboardFnKnown      0x4

/*
 * SFAppleTouchBar -> SFM1SPIHID, for tests: make up a keyboard input report with no key
 * down and the Fn bit as the argument's bit 0 says, and look at it as at a report the
 * keyboard sent (nothing is delivered to IOHIDFamily). kIOReturnUnsupported when the
 * keyboard's descriptor has no Fn usage.
 */
#define kSFTouchBarTestKeyboardReport   iokit_vendor_specific_msg(0x5443)

#define kSFTouchBarServiceClass         "SFAppleTouchBar"
#define kSFKeyboardServiceClass         "SFM1SPIHID"

#endif
