/* SPDX-License-Identifier: MIT */
/*
 * Where a usage sits in the input reports of a HID report descriptor: enough of a parser
 * to find the Fn key of Apple's built-in keyboard (usage page 0xff, usage 0x03, which
 * Linux hid-apple takes as its Fn key) in the report the keyboard sends. Plain C, shared
 * with the host tests.
 */
#ifndef _SF_HID_FN_H
#define _SF_HID_FN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bit offset, counted from the start of the report including its report ID byte when the
 * descriptor uses report IDs, of the first variable input field with this usage page and
 * usage in report `report_id`; *bits (may be NULL) gets the field's width. -1 when there
 * is none or the descriptor is malformed.
 */
int sf_hid_input_field(const uint8_t *descriptor, size_t length, unsigned report_id, unsigned usage_page, unsigned usage,
    unsigned *bits);

#ifdef __cplusplus
}
#endif
#endif
