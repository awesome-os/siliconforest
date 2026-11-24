/* SPDX-License-Identifier: MIT */
/* DESK K14 wire ABI. Read under the producer lock; no user pointers. */
#ifndef SF_TP_GESTURE_RING_H
#define SF_TP_GESTURE_RING_H
#include <stdint.h>
#define SF_TP_SWIPE_RING 32u
struct sf_tp_swipe_event {
    uint32_t seq;
    uint8_t phase, fingers;
    int16_t dx, dy;
    uint8_t synthetic, kind; /* 0 swipe, 1 pinch */
    uint32_t ms;
    uint32_t scale_q16;
    int32_t angle_cdeg;
};
struct sf_tp_swipe_ring {
    uint32_t version, next_seq;
    struct sf_tp_swipe_event ev[SF_TP_SWIPE_RING];
};
#if defined(__cplusplus)
static_assert(sizeof(sf_tp_swipe_event)==24 && sizeof(sf_tp_swipe_ring)==776,"K14 gesture ABI");
#else
_Static_assert(sizeof(struct sf_tp_swipe_event)==24 && sizeof(struct sf_tp_swipe_ring)==776,"K14 gesture ABI");
#endif
#endif
