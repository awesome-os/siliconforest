/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Apple SPI trackpad multitouch report decoder and pointer synthesis for
 * SFM1SPIHID (J293).
 *
 * The report layout (struct tp_mouse_report, struct tp_header, struct
 * tp_finger), the validity rules and the "skip touch_major == 0" filter are
 * ported from drivers/hid/hid-magicmouse.c of Omacom linux-aurora
 * (integration/aurora, 6a08a08e):
 *
 *   Apple "Magic" Wireless Mouse driver
 *   Copyright (c) 2010 Michael Poole <mdpoole@troilus.org>
 *   Copyright (c) 2010 Chase Douglas <chase.douglas@canonical.com>
 *
 * The pointer synthesis (right button on a two-finger click, wheel steps on a
 * two-finger drag) has no counterpart in that driver, which hands contacts to
 * user space; it is new code under the same licence.
 *
 * Pure C, no allocation, no I/O: built into the kext and into the host tests.
 */
#ifndef M1TP_MT_H
#define M1TP_MT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* hid-magicmouse.c: SPI_REPORT_ID, SPI_RESET_REPORT_ID, MAX_CONTACTS */
#define M1TP_MT_REPORT_ID 0x02u
#define M1TP_MT_RESET_REPORT_ID 0x60u
#define M1TP_MT_MAX_CONTACTS 16u
/* sizeof(struct tp_mouse_report), sizeof(struct tp_header), sizeof(struct tp_finger) */
#define M1TP_MT_MOUSE_BYTES 8u
#define M1TP_MT_HEADER_BYTES 38u
#define M1TP_MT_FINGER_BYTES 30u
#define M1TP_MT_MAX_REPORT_BYTES (M1TP_MT_MOUSE_BYTES + M1TP_MT_HEADER_BYTES + \
    M1TP_MT_MAX_CONTACTS * M1TP_MT_FINGER_BYTES)

/* SET_REPORT(feature) payload that requests touch data: feature_mt_trackpad2_usb
 * in magicmouse_enable_multitouch(), sent by spi-hid-apple-core.c as request
 * 0x52, report 0x02, response length 2, to interface 2. */
#define M1TP_MT_ENABLE_COMMAND 0x52u
#define M1TP_MT_ENABLE_REPORT 0x02u
#define M1TP_MT_ENABLE_RESPONSE_BYTES 2u
extern const uint8_t m1tp_mt_enable_payload[2];

/* The report the child HID device publishes in multitouch mode: report ID 2,
 * three buttons, relative X and Y (16 bits each: a fast stroke is more than
 * 127 counts per report), wheel (8 bits). */
#define M1TP_MT_POINTER_REPORT_BYTES 7u
#define M1TP_MT_POINTER_DESCRIPTOR_BYTES 66u
extern const uint8_t m1tp_mt_pointer_descriptor[M1TP_MT_POINTER_DESCRIPTOR_BYTES];

enum m1tp_mt_kind {
    M1TP_MT_KIND_INVALID = 0,  /* not a report this decoder accepts; dropped */
    M1TP_MT_KIND_BASIC = 1,    /* 8-byte pointer report of the basic mode */
    M1TP_MT_KIND_TOUCH = 2,    /* pointer header + vendor header + contacts */
    M1TP_MT_KIND_RESET = 3     /* controller reset: the mode must be requested again */
};

struct m1tp_mt_contact {
    int16_t x, y;              /* abs_x, abs_y as sent: y grows away from the user */
    int16_t rel_x, rel_y;      /* not used for synthesis; printed by the debug dump */
    int16_t touch_major, touch_minor, pressure;
};

struct m1tp_mt_frame {
    uint8_t kind;
    uint8_t mouse_buttons;     /* tp_mouse_report.buttons */
    int8_t mouse_dx, mouse_dy; /* tp_mouse_report.rel_x, rel_y */
    uint8_t tp_buttons;        /* tp_header.buttons */
    uint8_t reported;          /* tp_header.num_fingers */
    uint8_t contacts;          /* records with touch_major != 0 */
    uint16_t timestamp_ms;     /* tp_header bytes 4-5: the controller's millisecond counter
                                * (seen in boot mt01: +8 per report; not named by Linux) */
    struct m1tp_mt_contact c[M1TP_MT_MAX_CONTACTS];
};

/*
 * Every number below is taken from a source, none from feel:
 *  - geometry: what this M1's Linux reports for "Apple SPI Trackpad" (EVIOCGABS, read
 *    2026-11-10): X -6046..6536 at 98 units/mm, Y -7439..164 at 97 units/mm (128 x 78 mm).
 *    hid-magicmouse.c reads these from the controller (report 0xd9, magicmouse_open());
 *    Linux negates Y, the contacts here keep the controller's sign until m1tp_mt_step().
 *  - behaviour: libinput 1.32.0 (the release the M1's Arch/Omarchy ships; git 5cf833b),
 *    files named at each use in m1tp_mt.c, with its quirks for this device
 *    (quirks/50-system-apple.quirks "Apple Laptop Touchpad (SPI)": AttrTouchSizeRange=150:130,
 *    AttrPalmSizeThreshold=1600, ModelAppleTouchpad=1).
 *  - desktop settings: Omarchy's Hyprland defaults on that machine
 *    (/usr/share/omarchy/default/hypr/input.lua and ~/.config/hypr/input.lua): sensitivity 0,
 *    adaptive profile, clickfinger_behavior, scroll_factor 0.4, natural_scroll true,
 *    tap_to_click false, disable-while-typing on (Hyprland default); monitor scale 2.
 */
struct m1tp_mt_config {
    int32_t res_x, res_y;           /* units per mm: 98, 97 */
    int32_t min_x, max_x;           /* -6046, 6536 */
    int32_t min_y, max_y;           /* as sent by the controller: -164, 7439 */
    uint32_t touch_low, touch_high; /* 130, 150: touch size hysteresis, in Linux units (2 * raw) */
    uint32_t palm_size;             /* 1600: a larger touch is a palm, same units */
    uint32_t accel_threshold;       /* 130 mm/s: start of libinput's acceleration incline */
    uint32_t gain_percent;          /* 200: pointer counts per libinput unit, in percent. Linux
                                     * moves the pointer in logical pixels of a scale-2 output;
                                     * a count here is one panel pixel. */
    uint32_t flat_units_per_count;  /* 0; non-zero: no curve, this many contact units per count */
    uint32_t scroll_factor_permille;/* 400: Hyprland touchpad:scroll_factor */
    uint32_t scroll_step_px;        /* 15: scroll units that make one wheel detent */
    uint32_t scroll_units_per_step; /* 0; non-zero: this many contact units per wheel step */
    uint8_t natural_scroll;         /* 1: content follows the fingers */
    uint8_t dwt;                    /* 1: disable-while-typing */
    uint8_t thumb;                  /* 1: thumb detection */
    uint8_t use_header_motion;      /* 0; 1: once the pointer header of a touch report carries
                                     * motion, take the pointer motion from it */
};
/* m1tp_mt_init() takes a zero in a numeric field as "default". These say "off"
 * for the three switches that default to on. */
#define M1TP_MT_OFF 2u

struct m1tp_mt_out {
    uint8_t buttons;                /* bit 0 left, bit 1 right, bit 2 middle */
    int16_t dx, dy;                 /* dy grows towards the user */
    int8_t wheel;                   /* + is away from the user */
    /* A three- or four-finger swipe (not part of the pointer report; m1tp_mt_step() fills it
     * whether or not it returns a pointer report): M1TP_MT_SWIPE_*, the finger count, and for an
     * update the travel in libinput's units (accelerated like the pointer, normalized to 1000 dpi,
     * before this desktop's gain: what a Linux compositor receives as the swipe's dx/dy),
     * physical directions: x grows to the right, y towards the user. */
    uint8_t swipe, swipe_fingers;
    int16_t swipe_dx, swipe_dy;
    uint8_t gesture_kind;           /* 0 swipe, 1 pinch; phase uses swipe above */
    uint32_t scale_q16;             /* pinch scale since BEGIN, 65536 is 1.0 */
    int32_t angle_cdeg;             /* clockwise rotation since previous report */
};
#define M1TP_MT_SWIPE_NONE 0u
#define M1TP_MT_SWIPE_BEGIN 1u
#define M1TP_MT_SWIPE_UPDATE 2u
#define M1TP_MT_SWIPE_END 3u
#define M1TP_MT_GESTURE_CANCEL 4u

/* libinput's gesture states, as far as they are used */
enum m1tp_mt_gesture { M1TP_MT_G_NONE = 0, M1TP_MT_G_UNKNOWN = 1, M1TP_MT_G_POINTER = 2,
    M1TP_MT_G_SCROLL = 3, M1TP_MT_G_IGNORED = 4 /* five or more fingers */,
    M1TP_MT_G_SWIPE = 5 /* three or four fingers */, M1TP_MT_G_PINCH = 6 };
/* libinput's thumb states (evdev-mt-touchpad.h) */
enum m1tp_mt_thumb { M1TP_MT_T_FINGER = 0, M1TP_MT_T_JAILED, M1TP_MT_T_PINCH,
    M1TP_MT_T_SUPPRESSED, M1TP_MT_T_REVIVED, M1TP_MT_T_REVIVED_JAILED, M1TP_MT_T_DEAD };

/* One-shot facts for the driver's stage log lines. */
#define M1TP_MT_EV_HEADER_MOTION 1u /* a touch report's pointer header carried motion */
#define M1TP_MT_EV_RIGHT_CLICK 2u
#define M1TP_MT_EV_SCROLL 4u
#define M1TP_MT_EV_THUMB 8u         /* a resting thumb was recognised */
#define M1TP_MT_EV_PALM 16u         /* a touch was ignored as a palm (size or typing) */
#define M1TP_MT_EV_DEVICE_CLOCK 32u /* report intervals come from the controller's counter */

#define M1TP_MT_SLOTS 8u
struct m1tp_mt_slot {
    uint8_t used, seen, down, palm, pinned, exceeded, history;
    int32_t x, y;                   /* y negated: grows towards the user, as in libinput */
    int32_t prev_x, prev_y, dx, dy; /* this report's travel */
    int32_t start_x, start_y;       /* gesture.initial */
    int32_t pin_x, pin_y;
    int32_t speed;                  /* um/s */
    int32_t jump_last;              /* um per 12 ms */
    uint64_t begin_us, prev_us;
};

struct m1tp_mt_state {
    struct m1tp_mt_config cfg;
    struct m1tp_mt_slot slot[M1TP_MT_SLOTS];
    uint8_t fingers_down;
    uint8_t button_down, button_mask, emitted_buttons;
    uint8_t header_motion;          /* latched (use_header_motion) */
    uint8_t gesture, finger_count, finger_pending;
    uint8_t thumb_state, thumb_slot, pinch_eligible;
    uint8_t keyboard_active, keys_held, have_clock;
    uint16_t clock_ms;
    uint64_t now_us, last_report_us;
    uint64_t gesture_start_us, pending_since_us;
    uint64_t key_last_us, key_until_us;
    /* pointer acceleration (libinput filter-touchpad.c) */
    uint64_t accel_last_us;
    int64_t accel_last_velocity;    /* um/s */
    int32_t dpi;
    int64_t rem_x, rem_y;           /* 1/65536 count */
    int64_t rem_scroll;             /* 1/65536 scroll unit */
    uint8_t swipe_fingers;          /* a swipe is under way with this many fingers (0: none) */
    int64_t swipe_rem_x, swipe_rem_y; /* 1/65536 libinput unit */
    uint8_t pinch_active, pinch_a, pinch_b;
    uint64_t pinch_distance;
    int32_t pinch_angle, pinch_center_x, pinch_center_y;
    uint32_t pinch_scale_q16;
    int64_t scroll_per_unit;        /* scroll units per contact unit, 1/65536 */
    uint32_t events;                /* M1TP_MT_EV_* seen so far */
    /* measurements for the self-test and the debug dump */
    int64_t last_velocity;          /* um/s of the last pointer motion */
    int32_t last_factor;            /* its acceleration factor, 1/65536 */
};

void m1tp_mt_default_config(struct m1tp_mt_config *);
/* A zero field of *config takes its default (see M1TP_MT_OFF). */
void m1tp_mt_init(struct m1tp_mt_state *, const struct m1tp_mt_config *config);
/* Classify and decode one CRC-checked interface 2 input report. Always fills
 * *frame; returns frame->kind. */
int m1tp_mt_parse(const uint8_t *payload, size_t bytes, struct m1tp_mt_frame *frame);
/* Advance by one decoded frame received at time_us (any monotonic clock).
 * Returns 1 when *out is a pointer report to deliver, 0 when there is nothing
 * to report. */
int m1tp_mt_step(struct m1tp_mt_state *, const struct m1tp_mt_frame *, uint64_t time_us,
                 struct m1tp_mt_out *out);
/* Keyboard activity for disable-while-typing (libinput tp_keyboard_event()):
 * typing_key_pressed: a key that is neither a modifier nor one libinput
 * ignores went down; other_modifier_held: Ctrl, Alt or Cmd is down;
 * typing_key_held: such a key is still down. */
void m1tp_mt_key(struct m1tp_mt_state *, uint64_t time_us, int typing_key_pressed,
                 int other_modifier_held, int typing_key_held);
/* The same from a boot-protocol style keyboard report (ID, modifier bits,
 * reserved, six usages), as the J293 keyboard sends on interface 1. previous
 * holds the last report's six usages and is updated. */
void m1tp_mt_keyboard_report(struct m1tp_mt_state *, uint64_t time_us,
                             const uint8_t *report, size_t bytes, uint8_t previous[6]);
/* out -> the 5-byte report described by m1tp_mt_pointer_descriptor. */
void m1tp_mt_encode(const struct m1tp_mt_out *, uint8_t report[M1TP_MT_POINTER_REPORT_BYTES]);
/* libinput's touchpad_accel_profile_linear() at speed setting 0, without the
 * output gain: speed in um/s -> factor in 1/65536. For the tests. */
int32_t m1tp_mt_accel_factor(const struct m1tp_mt_state *, int64_t speed_um_s);

/* Fixture builder shared by the host tests and the in-kernel self-test: writes
 * a touch-mode report. major[] holds each contact's raw touch_major (minor is
 * 1.6 times it, as real fingers report); NULL gives every contact 170, a
 * fingertip as recorded in boot mt01. Returns the report length, or 0. */
size_t m1tp_mt_build(uint8_t *report, size_t capacity, uint8_t tp_buttons,
    uint16_t timestamp_ms, unsigned contacts, const int16_t (*xy)[2], const int16_t *major);

/* Runs the fixed scenarios (m1tp_mt_selftest.c) through parse + step on a
 * scratch state: contact sequences given in millimetres and milliseconds, one
 * of them recorded on this trackpad, with every emitted pointer report
 * compared. Returns 0 when all match, else the failing check's number.
 * *checks and *reports receive the counts; *measured (may be NULL) receives
 * the numbers the driver prints. A non-NULL sink also receives the pointer
 * reports of the first scenario, the one meant for delivery. */
struct m1tp_mt_measured {
    int32_t slow_counts, slow_um;       /* 10 mm/s for 20 mm */
    int32_t medium_counts, medium_um;   /* 100 mm/s for 40 mm */
    int32_t fast_counts, fast_um;       /* 400 mm/s for 80 mm */
    int32_t recorded_dx, recorded_dy;   /* the stroke recorded in boot mt01 */
    int32_t scroll_steps, scroll_um;    /* two fingers, 50 mm */
    int32_t failed_dx;                  /* pointer travel of a failing check */
};
typedef void (*m1tp_mt_sink)(void *context, const struct m1tp_mt_out *);
unsigned m1tp_mt_selftest(struct m1tp_mt_state *scratch, uint8_t *buffer, size_t capacity,
    unsigned *checks, unsigned *reports, struct m1tp_mt_measured *measured,
    m1tp_mt_sink sink, void *context);

#ifdef __cplusplus
}
#endif
#endif
