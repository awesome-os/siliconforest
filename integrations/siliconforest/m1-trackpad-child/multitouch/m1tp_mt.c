// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple SPI trackpad multitouch report decoder and pointer synthesis.
 *
 * Report layout and validity rules ported from drivers/hid/hid-magicmouse.c
 * (magicmouse_raw_event_spi, magicmouse_raw_event_mtp, struct tp_finger,
 * struct tp_header, struct tp_mouse_report) of Omacom linux-aurora
 * integration/aurora 6a08a08e:
 *
 *   Apple "Magic" Wireless Mouse driver
 *   Copyright (c) 2010 Michael Poole <mdpoole@troilus.org>
 *   Copyright (c) 2010 Chase Douglas <chase.douglas@canonical.com>
 *
 * Touch handling, thumb and palm rules, the two-finger gesture decision,
 * clickfinger and the pointer acceleration are ported from libinput 1.32.0
 * (git 5cf833b; src/evdev-mt-touchpad.c, -buttons.c, -gestures.c, -thumb.c,
 * filter.c, filter-touchpad.c, libinput-private.h), MIT licence:
 *
 *   Copyright © 2006-2009 Simon Thum
 *   Copyright © 2012 Jonas Ådahl
 *   Copyright © 2014-2019 Red Hat, Inc.
 *   Copyright © 2019 Matt Mayfield
 *
 * (full permission notice in NOTICE). libinput works in floating point and on
 * evdev slots; this is integer arithmetic on the controller's own records,
 * and only the parts the 12-byte pointer record can carry: no tapping (off on
 * the reference desktop), no horizontal scrolling. Pinches and three- and
 * four-finger swipes (libinput's swipe gesture) are given to the driver beside the
 * pointer report (m1tp_mt_out.swipe), for the desktop's switch between Spaces.
 */
#include "m1tp_mt.h"
#include <string.h>

/* feature_mt_trackpad2_usb in magicmouse_enable_multitouch() */
const uint8_t m1tp_mt_enable_payload[2] = { 0x02, 0x01 };

const uint8_t m1tp_mt_pointer_descriptor[M1TP_MT_POINTER_DESCRIPTOR_BYTES] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, /* mouse, pointer */
    0x85, 0x02,                                                 /* report ID 2 */
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, /* buttons 1-3 */
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,                         /* 5 bits padding */
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,                         /* X, Y */
    0x16, 0x01, 0x80, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x02, /* -32767..32767, 16 bits */
    0x81, 0x06,                                                 /* relative */
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x01, /* wheel, -127..127 */
    0x81, 0x06,
    0xc0, 0xc0
};

/* struct tp_header: num_fingers and buttons follow 22 unknown bytes. */
#define TP_HDR_TIMESTAMP 4u
#define TP_HDR_NUM_FINGERS 22u
#define TP_HDR_BUTTONS 23u
/* struct tp_finger, little-endian 16-bit fields */
#define TP_F_ABS_X 4u
#define TP_F_ABS_Y 6u
#define TP_F_REL_X 8u
#define TP_F_REL_Y 10u
#define TP_F_TOUCH_MAJOR 18u
#define TP_F_TOUCH_MINOR 20u
#define TP_F_PRESSURE 26u

/* le16_to_int() */
static int16_t le16s(const uint8_t *p)
{
    return (int16_t)(uint16_t)(p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static void put16(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)((uint16_t)v & 0xffu);
    p[1] = (uint8_t)((uint16_t)v >> 8);
}
static int64_t iabs64(int64_t v) { return v < 0 ? -v : v; }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

static uint64_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* Length of a travel in micrometres (evdev_device_unit_delta_to_mm + hypot). */
static int64_t travel_um(const struct m1tp_mt_state *s, int32_t dx, int32_t dy)
{
    int64_t x = (int64_t)dx * 1000 / s->cfg.res_x, y = (int64_t)dy * 1000 / s->cfg.res_y;
    return (int64_t)isqrt64((uint64_t)(x * x + y * y));
}

void m1tp_mt_default_config(struct m1tp_mt_config *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->res_x = 98; c->res_y = 97;
    c->min_x = -6046; c->max_x = 6536;
    c->min_y = -164; c->max_y = 7439;
    c->touch_low = 130; c->touch_high = 150;
    c->palm_size = 1600;
    c->accel_threshold = 130;
    c->gain_percent = 200;
    c->flat_units_per_count = 0;
    c->scroll_factor_permille = 400;
    c->scroll_step_px = 15;
    c->scroll_units_per_step = 0;
    c->natural_scroll = 1; c->dwt = 1; c->thumb = 1;
    c->use_header_motion = 0;
}

void m1tp_mt_init(struct m1tp_mt_state *s, const struct m1tp_mt_config *config)
{
    struct m1tp_mt_config d;
    if (!s) return;
    memset(s, 0, sizeof(*s));
    m1tp_mt_default_config(&d);
    if (config) {
        if (config->res_x > 0 && config->res_x < 1000) d.res_x = config->res_x;
        if (config->res_y > 0 && config->res_y < 1000) d.res_y = config->res_y;
        if (config->max_x > config->min_x) { d.min_x = config->min_x; d.max_x = config->max_x; }
        if (config->max_y > config->min_y) { d.min_y = config->min_y; d.max_y = config->max_y; }
        if (config->touch_low) d.touch_low = config->touch_low;
        if (config->touch_high) d.touch_high = config->touch_high;
        if (config->palm_size) d.palm_size = config->palm_size;
        if (config->accel_threshold) d.accel_threshold = config->accel_threshold;
        if (config->gain_percent) d.gain_percent = config->gain_percent;
        d.flat_units_per_count = config->flat_units_per_count;
        if (config->scroll_factor_permille) d.scroll_factor_permille = config->scroll_factor_permille;
        if (config->scroll_step_px) d.scroll_step_px = config->scroll_step_px;
        d.scroll_units_per_step = config->scroll_units_per_step;
        if (config->natural_scroll) d.natural_scroll = config->natural_scroll == 1;
        if (config->dwt) d.dwt = config->dwt == 1;
        if (config->thumb) d.thumb = config->thumb == 1;
        d.use_header_motion = config->use_header_motion == 1;
    }
    /* keep the arithmetic far from overflow whatever a boot-arg says */
    if (d.accel_threshold > 10000) d.accel_threshold = 10000;
    if (d.gain_percent > 5000) d.gain_percent = 5000;
    if (d.flat_units_per_count > 1000) d.flat_units_per_count = 1000;
    if (d.scroll_factor_permille > 100000) d.scroll_factor_permille = 100000;
    if (d.scroll_step_px > 10000) d.scroll_step_px = 10000;
    if (d.scroll_units_per_step > 100000) d.scroll_units_per_step = 100000;
    s->cfg = d;
    /* evdev-mt-touchpad.c tp_init(): "device->dpi = absinfo_x->resolution * 25.4" (an int) */
    s->dpi = d.res_x * 254 / 10;
    /* Scroll units per contact unit of vertical travel. libinput: tp_filter_scroll()
     * -> touchpad_constant_filter(): normalize to 1000 dpi (x_scale_coeff /
     * y_scale_coeff in tp_init_accel()), times baseline 0.9, times
     * TP_MAGIC_SLOWDOWN 0.2968; then the compositor's scroll_factor. */
    if (d.scroll_units_per_step)
        s->scroll_per_unit = ((int64_t)d.scroll_step_px << 16) / d.scroll_units_per_step;
    else
        s->scroll_per_unit = ((int64_t)10000 << 16) / (254 * d.res_y) * 26712 / 100000 *
                             d.scroll_factor_permille / 1000;
    s->thumb_slot = 0xff;
    s->pinch_eligible = 1;
}

int m1tp_mt_parse(const uint8_t *data, size_t size, struct m1tp_mt_frame *f)
{
    const size_t prefix = M1TP_MT_MOUSE_BYTES + M1TP_MT_HEADER_BYTES;
    const uint8_t *tp;
    size_t npoints, i;
    unsigned n = 0;

    if (!f) return M1TP_MT_KIND_INVALID;
    memset(f, 0, sizeof(*f));
    if (!data || !size) return M1TP_MT_KIND_INVALID;
    /* magicmouse_raw_event_spi(): the reset report restarts the mode request */
    if (data[0] == M1TP_MT_RESET_REPORT_ID) {
        f->kind = M1TP_MT_KIND_RESET;
        return f->kind;
    }
    if (data[0] != M1TP_MT_REPORT_ID || size < M1TP_MT_MOUSE_BYTES)
        return M1TP_MT_KIND_INVALID;
    f->mouse_buttons = data[1];
    /* -128 is outside the descriptor's -127..127 and has been seen in idle
     * basic-mode reports: not motion. */
    f->mouse_dx = data[2] == 0x80 ? 0 : (int8_t)data[2];
    f->mouse_dy = data[3] == 0x80 ? 0 : (int8_t)data[3];
    /* The basic mode sends the 8-byte pointer report alone. */
    if (size == M1TP_MT_MOUSE_BYTES) {
        f->kind = M1TP_MT_KIND_BASIC;
        return f->kind;
    }
    /* magicmouse_raw_event_mtp(): "Expect 46 bytes of prefix, and N * 30
     * bytes of touch data." */
    if (size < prefix || ((size - prefix) % M1TP_MT_FINGER_BYTES) != 0)
        return M1TP_MT_KIND_INVALID;
    tp = data + M1TP_MT_MOUSE_BYTES;
    npoints = (size - prefix) / M1TP_MT_FINGER_BYTES;
    if (npoints < tp[TP_HDR_NUM_FINGERS] || npoints > M1TP_MT_MAX_CONTACTS)
        return M1TP_MT_KIND_INVALID;
    f->timestamp_ms = (uint16_t)(tp[TP_HDR_TIMESTAMP] | (uint16_t)((uint16_t)tp[TP_HDR_TIMESTAMP + 1] << 8));
    f->reported = tp[TP_HDR_NUM_FINGERS];
    f->tp_buttons = tp[TP_HDR_BUTTONS];
    for (i = 0; i < f->reported; i++) {
        const uint8_t *r = data + prefix + i * M1TP_MT_FINGER_BYTES;
        struct m1tp_mt_contact *c = &f->c[n];
        /* a record with no touch area is not a contact */
        if (le16s(r + TP_F_TOUCH_MAJOR) == 0)
            continue;
        c->x = le16s(r + TP_F_ABS_X);
        c->y = le16s(r + TP_F_ABS_Y);
        c->rel_x = le16s(r + TP_F_REL_X);
        c->rel_y = le16s(r + TP_F_REL_Y);
        c->touch_major = le16s(r + TP_F_TOUCH_MAJOR);
        c->touch_minor = le16s(r + TP_F_TOUCH_MINOR);
        c->pressure = le16s(r + TP_F_PRESSURE);
        n++;
    }
    f->contacts = (uint8_t)n;
    f->kind = M1TP_MT_KIND_TOUCH;
    return f->kind;
}

void m1tp_mt_encode(const struct m1tp_mt_out *o, uint8_t report[M1TP_MT_POINTER_REPORT_BYTES])
{
    report[0] = M1TP_MT_REPORT_ID;
    report[1] = (uint8_t)(o->buttons & 7u);
    put16(report + 2, o->dx);
    put16(report + 4, o->dy);
    report[6] = (uint8_t)o->wheel;
}

/* ---- pointer acceleration: libinput filter-touchpad.c ------------------- */

/* touchpad_accel_profile_linear() without TP_MAGIC_SLOWDOWN: factor in 1/65536
 * for a speed in um/s. */
static int64_t accel_profile(const struct m1tp_mt_state *s, int64_t speed)
{
    const int64_t threshold = (int64_t)s->cfg.accel_threshold * 1000;
    const int64_t baseline = 58982;                 /* 0.9 */
    int64_t factor;

    if (speed < 7000) {
        /* "y = 0.1x + 0.3": decelerate down to 30 % */
        factor = speed * 65536 / 10000 + 19661;
        if (factor > baseline) factor = baseline;
    } else if (speed < threshold) {
        factor = baseline;
    } else {
        /* "factor = 0.0025 * (speed_in / threshold) * (speed_in - threshold) + baseline",
         * speed capped at four times the threshold */
        if (speed > threshold * 4) speed = threshold * 4;
        factor = speed * (speed - threshold) / threshold * 16384 / 100000 + baseline;
    }
    return factor;                                  /* speed_factor(0) == 1 */
}

int32_t m1tp_mt_accel_factor(const struct m1tp_mt_state *s, int64_t speed_um_s)
{
    /* "return factor * TP_MAGIC_SLOWDOWN" (0.2968) */
    return (int32_t)(accel_profile(s, speed_um_s) * 2968 / 10000);
}

/* accelerator_filter_touchpad(): the report's travel in contact units (dy
 * already towards the user) -> pointer counts, carrying the fraction. */
static void accelerate_gain(struct m1tp_mt_state *s, int32_t ux, int32_t uy, uint64_t now,
                            uint32_t gain_percent, int64_t *rem_x, int64_t *rem_y,
                            int32_t *dx, int32_t *dy);
static void accelerate(struct m1tp_mt_state *s, int32_t ux, int32_t uy, uint64_t now,
                       int32_t *dx, int32_t *dy)
{
    accelerate_gain(s, ux, uy, now, s->cfg.gain_percent, &s->rem_x, &s->rem_y, dx, dy);
}
/* The same with a given gain and remainder: a swipe's travel is libinput's own units
 * (gain 100), carried apart from the pointer's fraction. */
static void accelerate_gain(struct m1tp_mt_state *s, int32_t ux, int32_t uy, uint64_t now,
                            uint32_t gain_percent, int64_t *rem_x, int64_t *rem_y,
                            int32_t *dx, int32_t *dy)
{
    int64_t x, y, len, tdelta, velocity, factor;

    *dx = *dy = 0;
    /* tp_filter_motion(): "if (device_float_is_zero(*unaccelerated)) return zero" */
    if (!ux && !uy) return;
    if (s->cfg.flat_units_per_count) {
        int64_t k = (int64_t)s->cfg.flat_units_per_count << 16;
        *rem_x += ((int64_t)ux << 16) * gain_percent / s->cfg.gain_percent;
        *rem_y += ((int64_t)uy << 16) * gain_percent / s->cfg.gain_percent;
        *dx = (int32_t)(*rem_x / k); *rem_x -= (int64_t)*dx * k;
        *dy = (int32_t)(*rem_y / k); *rem_y -= (int64_t)*dy * k;
        return;
    }
    /* tp_scale_to_xaxis(): y in the x axis' resolution; 1/256 unit */
    x = (int64_t)ux * 256;
    y = (int64_t)uy * 256 * s->cfg.res_x / s->cfg.res_y;
    len = (int64_t)isqrt64((uint64_t)(x * x + y * y));
    /* trackers_velocity() with the touchpad's two trackers: this travel over
     * the time since the previous one; after MOTION_TIMEOUT (1 s) over 1 s. */
    tdelta = (int64_t)(now - s->accel_last_us);
    if (now < s->accel_last_us || tdelta > 1000000) tdelta = 1000000;
    tdelta += 1;
    s->accel_last_us = now;
    /* units/us -> um/s as the profile does: "v_us2s(speed_in) * 25.4 / dpi" */
    velocity = len * 1000000 / tdelta * 25400 / (256 * (int64_t)s->dpi);
    /* calculate_acceleration_simpsons() */
    factor = (accel_profile(s, velocity) + accel_profile(s, s->accel_last_velocity) +
              4 * accel_profile(s, (velocity + s->accel_last_velocity) / 2)) / 6;
    factor = factor * 2968 / 10000;
    s->accel_last_velocity = velocity;
    s->last_velocity = velocity; s->last_factor = (int32_t)factor;
    /* normalize_for_dpi(): "* DEFAULT_MOUSE_DPI / dpi"; then this desktop's gain */
    *rem_x += x * factor / 256 * 1000 / s->dpi * gain_percent / 100;
    *rem_y += y * factor / 256 * 1000 / s->dpi * gain_percent / 100;
    *dx = (int32_t)(*rem_x / 65536); *rem_x -= (int64_t)*dx * 65536;
    *dy = (int32_t)(*rem_y / 65536); *rem_y -= (int64_t)*dy * 65536;
}

/* ---- thumbs: libinput evdev-mt-touchpad-thumb.c -------------------------- */

#define THUMB_NONE 0xffu
/* SCROLL_MM_X, SCROLL_MM_Y, THUMB_TIMEOUT */
#define SCROLL_MM_X 35
#define SCROLL_MM_Y 25
#define THUMB_TIMEOUT_US 100000

/* tp_init_thumb(): "mm.y = h * 0.85" / "h * 0.92" from the top edge */
static int32_t thumb_line(const struct m1tp_mt_state *s, int permille)
{
    int32_t top = -s->cfg.max_y, bottom = -s->cfg.min_y;
    return top + (int32_t)((int64_t)(bottom - top) * permille / 1000);
}
static void thumb_set(struct m1tp_mt_state *s, unsigned slot, uint8_t state)
{
    s->thumb_state = state; s->thumb_slot = (uint8_t)slot;
    if (state != M1TP_MT_T_FINGER) s->events |= M1TP_MT_EV_THUMB;
}
static void thumb_lift(struct m1tp_mt_state *s)
{ s->thumb_state = M1TP_MT_T_FINGER; s->thumb_slot = THUMB_NONE; }
/* tp_thumb_needs_jail(); this device has no thumb size or pressure quirk */
static int thumb_needs_jail(const struct m1tp_mt_state *s, const struct m1tp_mt_slot *t)
{
    if (t->y < thumb_line(s, 850)) return 0;
    if (t->exceeded >= 10) return 0;
    return 1;
}
static int thumb_ignored(const struct m1tp_mt_state *s, unsigned slot)
{
    return s->cfg.thumb && s->thumb_slot == slot &&
        (s->thumb_state == M1TP_MT_T_JAILED || s->thumb_state == M1TP_MT_T_PINCH ||
         s->thumb_state == M1TP_MT_T_SUPPRESSED || s->thumb_state == M1TP_MT_T_REVIVED_JAILED ||
         s->thumb_state == M1TP_MT_T_DEAD);
}
static int thumb_ignored_for_gesture(const struct m1tp_mt_state *s, unsigned slot)
{
    return s->cfg.thumb && s->thumb_slot == slot &&
        (s->thumb_state == M1TP_MT_T_JAILED || s->thumb_state == M1TP_MT_T_SUPPRESSED ||
         s->thumb_state == M1TP_MT_T_REVIVED_JAILED || s->thumb_state == M1TP_MT_T_DEAD);
}
static void thumb_suppress(struct m1tp_mt_state *s, unsigned slot)
{
    if (s->thumb_state == M1TP_MT_T_FINGER || s->thumb_state == M1TP_MT_T_JAILED ||
        s->thumb_state == M1TP_MT_T_PINCH || s->thumb_slot != slot)
        thumb_set(s, slot, M1TP_MT_T_SUPPRESSED);
    else
        thumb_set(s, slot, M1TP_MT_T_DEAD);
}
static void thumb_pinch(struct m1tp_mt_state *s, unsigned slot)
{
    if (s->thumb_state == M1TP_MT_T_FINGER || s->thumb_state == M1TP_MT_T_JAILED ||
        s->thumb_slot != slot)
        thumb_set(s, slot, M1TP_MT_T_PINCH);
    else if (s->thumb_state != M1TP_MT_T_PINCH)
        thumb_suppress(s, slot);
}
/* tp_thumb_update_touch() */
static void thumb_update_touch(struct m1tp_mt_state *s, unsigned slot, int begin)
{
    struct m1tp_mt_slot *t = &s->slot[slot];
    if (!s->cfg.thumb) return;
    if (t->exceeded >= 10 && s->pinch_eligible && s->gesture == M1TP_MT_G_NONE) {
        s->pinch_eligible = 0;
        if (s->thumb_state == M1TP_MT_T_PINCH) s->thumb_state = M1TP_MT_T_SUPPRESSED;
    }
    if (s->fingers_down > 1) return;
    /* tp_thumb_revive() */
    if ((s->thumb_state == M1TP_MT_T_SUPPRESSED || s->thumb_state == M1TP_MT_T_PINCH) &&
        s->thumb_slot == slot)
        thumb_set(s, slot, thumb_needs_jail(s, t) ? M1TP_MT_T_REVIVED_JAILED : M1TP_MT_T_REVIVED);
    if (begin && thumb_needs_jail(s, t)) { thumb_set(s, slot, M1TP_MT_T_JAILED); return; }
    if (s->thumb_slot != slot) return;
    if (s->thumb_state == M1TP_MT_T_JAILED && !thumb_needs_jail(s, t))
        thumb_set(s, slot, M1TP_MT_T_FINGER);
    if (s->thumb_state == M1TP_MT_T_REVIVED_JAILED && !thumb_needs_jail(s, t))
        thumb_set(s, slot, M1TP_MT_T_REVIVED);
}
/* tp_thumb_update_multifinger(), run when a touch begins with another one down */
static void thumb_update_multifinger(struct m1tp_mt_state *s, unsigned newest)
{
    int first = -1, second = -1, oldest = -1;
    unsigned i, exceeded = 0;
    int64_t mm_x, mm_y;

    for (i = 0; i < M1TP_MT_SLOTS; i++) {
        const struct m1tp_mt_slot *t = &s->slot[i];
        if (!t->used || !t->down) continue;
        if (t->exceeded > exceeded) exceeded = t->exceeded;
        if (oldest < 0 || t->begin_us < s->slot[oldest].begin_us) oldest = (int)i;
        if (first < 0) { first = (int)i; continue; }
        if (t->y > s->slot[first].y) { second = first; first = (int)i; continue; }
        if (second < 0 || t->y > s->slot[second].y) second = (int)i;
    }
    if (first < 0 || second < 0) return;
    mm_x = iabs(s->slot[first].x - s->slot[second].x) / s->cfg.res_x;
    mm_y = iabs(s->slot[first].y - s->slot[second].y) / s->cfg.res_y;
    /* speed-based: a touch landing far from a finger that is moving */
    if (s->thumb_state == M1TP_MT_T_FINGER && s->fingers_down >= 2 && exceeded > 5 &&
        (mm_x > SCROLL_MM_X || mm_y > SCROLL_MM_Y)) {
        thumb_suppress(s, newest);
        return;
    }
    /* close together: both live, for two-finger scrolling */
    if (mm_x < SCROLL_MM_X && mm_y < SCROLL_MM_Y) { thumb_lift(s); return; }
    /* landed together above the lower thumb line: all live */
    if (s->slot[newest].begin_us - s->slot[oldest].begin_us < THUMB_TIMEOUT_US &&
        s->slot[first].y < thumb_line(s, 920)) { thumb_lift(s); return; }
    if (mm_y > SCROLL_MM_Y) {
        if (s->pinch_eligible) thumb_pinch(s, (unsigned)first);
        else thumb_suppress(s, (unsigned)first);
    } else {
        thumb_lift(s);
    }
}

/* ---- disable-while-typing: libinput evdev-mt-touchpad.c ------------------ */

/* DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_1 / _2 */
#define DWT_TIMEOUT_1_US 200000
#define DWT_TIMEOUT_2_US 500000
/* Not libinput: a key report that never clears must not disable the pad for
 * good. libinput keeps the timer running while any key is held. */
#define DWT_HELD_LIMIT_US 5000000

/* tp_keyboard_timeout(): while a key is held the timeout starts again */
static void dwt_expire(struct m1tp_mt_state *s, uint64_t now)
{
    if (!s->keyboard_active || now < s->key_until_us) return;
    if (s->keys_held && now - s->key_last_us < DWT_HELD_LIMIT_US)
        s->key_until_us = now + DWT_TIMEOUT_2_US;
    else
        s->keyboard_active = 0;
}

void m1tp_mt_key(struct m1tp_mt_state *s, uint64_t now, int pressed, int other_modifier, int held)
{
    unsigned i;
    if (!s || !s->cfg.dwt) return;
    dwt_expire(s, now);
    s->keys_held = held ? 1 : 0;
    if (!pressed) return;
    /* tp_keyboard_event() */
    if (!s->keyboard_active) {
        /* "don't trigger if we have an ongoing action that can't possibly be
         * an accidental touch" */
        if (s->gesture == M1TP_MT_G_SCROLL || s->button_down) return;
        /* tp_pointer_motion_is_active(): one finger, down for 500 ms */
        if (s->fingers_down == 1)
            for (i = 0; i < M1TP_MT_SLOTS; i++)
                if (s->slot[i].used && s->slot[i].down && !s->slot[i].palm &&
                    s->gesture == M1TP_MT_G_POINTER && now - s->slot[i].begin_us >= 500000)
                    return;
        /* "If any modifier is down we don't trigger dwt" */
        if (other_modifier) return;
        s->keyboard_active = 1;
        s->key_until_us = now + DWT_TIMEOUT_1_US;
    } else {
        s->key_until_us = now + DWT_TIMEOUT_2_US;
    }
    s->key_last_us = now;
}

void m1tp_mt_keyboard_report(struct m1tp_mt_state *s, uint64_t now, const uint8_t *r,
                             size_t n, uint8_t previous[6])
{
    int pressed = 0, held = 0;
    unsigned i, j;
    if (!s || !r || !previous || n < 9) return;
    for (i = 0; i < 6; i++) {
        uint8_t u = r[3 + i];
        int was = 0;
        /* tp_key_is_modifier() / tp_key_ignore_for_dwt(): letters, digits,
         * punctuation, Return, Backspace and Space count; Escape, Tab, Caps
         * Lock, the function keys and everything after them do not. */
        if (u < 0x04 || u > 0x38 || u == 0x29 || u == 0x2b) continue;
        held = 1;
        for (j = 0; j < 6; j++) if (previous[j] == u) was = 1;
        if (!was) pressed = 1;
    }
    for (i = 0; i < 6; i++) previous[i] = r[3 + i];
    /* Ctrl, Alt, Cmd on either side; Shift (0x02, 0x20) is exempt */
    m1tp_mt_key(s, now, pressed, (r[1] & 0xdd) != 0, held);
}

/* ---- touches ------------------------------------------------------------- */

static void touch_end(struct m1tp_mt_state *s, unsigned i)
{
    struct m1tp_mt_slot *t = &s->slot[i];
    if (!t->down) return;
    t->down = 0; t->palm = 0; t->pinned = 0; t->exceeded = 0; t->history = 0;
    if (s->fingers_down) s->fingers_down--;
    if (s->thumb_slot == i) thumb_lift(s);      /* tp_thumb_update_touch(): TOUCH_END */
}
/* tp_touch_active_for_gesture() */
static int active_for_gesture(const struct m1tp_mt_state *s, unsigned i)
{
    const struct m1tp_mt_slot *t = &s->slot[i];
    return t->used && t->down && !t->palm && !t->pinned && !thumb_ignored_for_gesture(s, i);
}

/* xy_get_direction() (libinput-private.h): the one or two octants of a travel
 * given in um; bit 0 is away from the user, clockwise from there. */
static uint32_t direction(int64_t x, int64_t y)
{
    int64_t ax = iabs64(x), ay = iabs64(y), major, minor;
    unsigned k, first;
    int rising;      /* the angle inside the octant grows with minor/major */

    if (ax < 2000 && ay < 2000) {
        if (x > 0 && y > 0) return 16 | 8 | 4;      /* S SE E */
        if (x > 0 && y < 0) return 1 | 2 | 4;       /* N NE E */
        if (x < 0 && y > 0) return 16 | 32 | 64;    /* S SW W */
        if (x < 0 && y < 0) return 1 | 128 | 64;    /* N NW W */
        if (x > 0) return 2 | 4 | 8;
        if (x < 0) return 128 | 64 | 32;
        if (y > 0) return 8 | 16 | 32;
        if (y < 0) return 2 | 1 | 128;
        return 0xff;                                /* UNDEFINED_DIRECTION */
    }
    /* octant k: [k * 45, k * 45 + 45) degrees clockwise from north */
    if (y < 0 && x >= 0) { k = ax < ay ? 0 : 1; }
    else if (x > 0 && y >= 0) { k = ay < ax ? 2 : 3; }
    else if (y > 0 && x <= 0) { k = ax < ay ? 4 : 5; }
    else { k = ay < ax ? 6 : 7; }
    major = ax > ay ? ax : ay; minor = ax > ay ? ay : ax;
    rising = !(k & 1);
    /* "d1 = (int)(r + 0.9) % 8; d2 = (int)(r + 0.1) % 8": within a tenth of an
     * octant (4.5 degrees: tan 0.0787, tan 40.5 = 0.8541) of a border only
     * one octant is marked */
    if (rising) {
        if (minor * 10000 < major * 787) first = 1;         /* frac < 0.1 */
        else if (minor * 10000 >= major * 8541) first = 2;  /* frac >= 0.9 */
        else first = 0;
    } else {
        if (minor * 10000 >= major * 8541) first = 1;
        else if (minor * 10000 < major * 787) first = 2;
        else first = 0;
    }
    if (first == 1) return 1u << k;
    if (first == 2) return 1u << ((k + 1) & 7);
    return (1u << k) | (1u << ((k + 1) & 7));
}
/* tp_gesture_same_directions() */
static int same_directions(uint32_t a, uint32_t b)
{
    return ((a | (a >> 1)) & b) || ((b | (b >> 1)) & a) ||
           ((a & 0x80) && (b & 0x01)) || ((b & 0x80) && (a & 0x01));
}

static void gesture_begin(struct m1tp_mt_state *s, unsigned count, uint64_t now)
{
    unsigned i;
    s->finger_count = (uint8_t)count; s->finger_pending = 0;
    s->gesture_start_us = now;
    s->gesture = count == 1 ? M1TP_MT_G_POINTER : count == 2 ? M1TP_MT_G_UNKNOWN :
                 count <= 4 ? M1TP_MT_G_SWIPE : M1TP_MT_G_IGNORED;
    for (i = 0; i < M1TP_MT_SLOTS; i++) { s->slot[i].start_x = s->slot[i].x; s->slot[i].start_y = s->slot[i].y; }
    s->rem_scroll = 0;
}

/* tp_gesture_detect_motion_gestures() for two fingers, gestures enabled */
static void gesture_detect(struct m1tp_mt_state *s, uint64_t now)
{
    int a = -1, b = -1, thumb, finger;
    unsigned i;
    int64_t a_um, b_um, thumb_um, finger_um, dist_x, dist_y;
    const int64_t min_move = 1500, max_move = 4000;

    for (i = 0; i < M1TP_MT_SLOTS; i++) {
        if (!active_for_gesture(s, i)) continue;
        if (a < 0) a = (int)i; else if (b < 0) b = (int)i;
    }
    if (a < 0 || b < 0) return;
    a_um = travel_um(s, s->slot[a].x - s->slot[a].start_x, s->slot[a].y - s->slot[a].start_y);
    b_um = travel_um(s, s->slot[b].x - s->slot[b].start_x, s->slot[b].y - s->slot[b].start_y);
    dist_x = (int64_t)iabs(s->slot[a].x - s->slot[b].x) * 1000 / s->cfg.res_x;
    dist_y = (int64_t)iabs(s->slot[a].y - s->slot[b].y) * 1000 / s->cfg.res_y;
    /* "If both touches moved less than a mm, we cannot decide yet" */
    if (a_um < 1000 && b_um < 1000) return;
    /* "within 7mm vertically and 40mm horizontally past the timeout, assume
     * scroll/swipe" (DEFAULT_GESTURE_SWIPE_TIMEOUT, 150 ms) */
    if (dist_x < 40000 && dist_y < 7000 && now - s->gesture_start_us > 150000) {
        s->gesture = M1TP_MT_G_SCROLL; return;
    }
    if (a_um >= max_move || b_um >= max_move) {
        /* "Pick the thumb as the lowest point on the touchpad" */
        if (s->slot[a].y > s->slot[b].y) { thumb = a; thumb_um = a_um; finger = b; finger_um = b_um; }
        else { thumb = b; thumb_um = b_um; finger = a; finger_um = a_um; }
        (void)finger;
        /* thumb still while the finger moves: a resting thumb */
        if (s->cfg.thumb && thumb_um < min_move) {
            thumb_suppress(s, (unsigned)thumb);
            s->gesture = M1TP_MT_G_NONE; s->finger_count = 0;   /* tp_gesture_cancel() */
            return;
        }
        /* finger still while the thumb moves: "one-finger scrolling" */
        if (finger_um < min_move) { s->gesture = M1TP_MT_G_SCROLL; return; }
    }
    if (a_um < min_move || b_um < min_move) return;
    if (same_directions(
            direction((int64_t)(s->slot[a].x - s->slot[a].start_x) * 1000 / s->cfg.res_x,
                      (int64_t)(s->slot[a].y - s->slot[a].start_y) * 1000 / s->cfg.res_y),
            direction((int64_t)(s->slot[b].x - s->slot[b].start_x) * 1000 / s->cfg.res_x,
                      (int64_t)(s->slot[b].y - s->slot[b].start_y) * 1000 / s->cfg.res_y)))
        s->gesture = M1TP_MT_G_SCROLL;
    else
        s->gesture = M1TP_MT_G_PINCH;
}

/* libinput tp_gesture_get_pinch_info/handle_state_pinch, in fixed point.
 * Coordinates are normalized to physical micrometres before distance/angle.
 * Integer CORDIC avoids floating point in the kernel; angle is clockwise
 * because our y grows towards the user. The constants are atan(2^-i), in
 * millionths of a degree, rounded independently of the contact decoder.
 */
static int32_t pinch_angle(int64_t x, int64_t y)
{
    static const int32_t angles[] = {45000000,26565051,14036243,7125016,
        3576334,1789911,895174,447614,223811,111906,55953,27976,13988,6994,3497,1749};
    int32_t angle = 0;
    unsigned i;
    x *= 1048576; y *= 1048576;
    if (x < 0) { angle = y >= 0 ? 180000000 : -180000000; x = -x; y = -y; }
    for (i = 0; i < sizeof(angles)/sizeof(angles[0]) && y; ++i) {
        int64_t dx = x / ((int64_t)1 << i), dy = y / ((int64_t)1 << i);
        if (y > 0) { x += dy; y -= dx; angle += angles[i]; }
        else { x -= dy; y += dx; angle -= angles[i]; }
    }
    return angle / 10000;
}

static void pinch_end(struct m1tp_mt_state *s, struct m1tp_mt_out *out, unsigned phase)
{
    out->swipe = (uint8_t)phase; out->swipe_fingers = 2; out->gesture_kind = 1;
    out->scale_q16 = s->pinch_scale_q16; s->pinch_active = 0;
}

static void pinch_step(struct m1tp_mt_state *s, unsigned count, uint64_t now,
                       struct m1tp_mt_out *out)
{
    unsigned i, a = M1TP_MT_SLOTS, b = M1TP_MT_SLOTS;
    int64_t vx, vy;
    uint64_t distance, scale;
    int32_t angle, center_x, center_y, delta, dx = 0, dy = 0;
    if (s->pinch_active && (s->gesture != M1TP_MT_G_PINCH || !count || s->finger_count != 2)) {
        unsigned phase = s->button_down || s->fingers_down >= 2 ?
            M1TP_MT_GESTURE_CANCEL : M1TP_MT_SWIPE_END;
        pinch_end(s,out,phase);
        return;
    }
    if (out->swipe || s->gesture != M1TP_MT_G_PINCH || count != 2) return; /* debounce: freeze values */
    for (i = 0; i < M1TP_MT_SLOTS; ++i) if (active_for_gesture(s, i)) {
        if (a == M1TP_MT_SLOTS) a = i; else if (b == M1TP_MT_SLOTS) b = i;
    }
    if (a == M1TP_MT_SLOTS || b == M1TP_MT_SLOTS) return;
    if (s->pinch_active && (a != s->pinch_a || b != s->pinch_b)) {
        pinch_end(s, out, M1TP_MT_GESTURE_CANCEL); s->gesture = M1TP_MT_G_NONE;s->finger_count = 0;
        return;
    }
    vx = (int64_t)(s->slot[b].x-s->slot[a].x)*1000/s->cfg.res_x;
    vy = (int64_t)(s->slot[b].y-s->slot[a].y)*1000/s->cfg.res_y;
    distance = isqrt64((uint64_t)(vx*vx+vy*vy));
    if (distance < 1000) { /* coincident contacts have no meaningful angle/scale */
        if (s->pinch_active) pinch_end(s,out,M1TP_MT_GESTURE_CANCEL);
        s->gesture = M1TP_MT_G_IGNORED; return;
    }
    angle = pinch_angle(vx,vy);
    center_x = (s->slot[a].x+s->slot[b].x)/2;
    center_y = (s->slot[a].y+s->slot[b].y)/2;
    if (!s->pinch_active) {
        s->pinch_active = 1;s->pinch_a = (uint8_t)a;s->pinch_b = (uint8_t)b;
        s->pinch_distance = distance;s->pinch_angle = angle;
        s->pinch_center_x = center_x;s->pinch_center_y = center_y;s->pinch_scale_q16 = 65536;
        s->swipe_rem_x = s->swipe_rem_y = 0;s->accel_last_us = now;
        out->swipe = M1TP_MT_SWIPE_BEGIN;out->swipe_fingers = 2;out->gesture_kind = 1;
        return;
    }
    scale = distance*65536/s->pinch_distance;
    if (scale > UINT32_MAX) scale = UINT32_MAX;
    delta = angle-s->pinch_angle;
    if (delta > 18000) delta -= 36000; else if (delta < -18000) delta += 36000;
    accelerate_gain(s,center_x-s->pinch_center_x,center_y-s->pinch_center_y,now,100,
                    &s->swipe_rem_x,&s->swipe_rem_y,&dx,&dy);
    if (dx || dy || scale != s->pinch_scale_q16 || delta) {
        out->swipe = M1TP_MT_SWIPE_UPDATE;out->swipe_fingers = 2;out->gesture_kind = 1;
        out->swipe_dx = (int16_t)(dx>32767?32767:dx < -32767?-32767:dx);
        out->swipe_dy = (int16_t)(dy>32767?32767:dy < -32767?-32767:dy);
        out->scale_q16 = (uint32_t)scale;out->angle_cdeg = delta;
    }
    s->pinch_angle = angle;s->pinch_center_x = center_x;s->pinch_center_y = center_y;
    s->pinch_scale_q16 = (uint32_t)scale;
}

/* tp_clickfinger_set_button() and tp_clickfinger_within_distance() */
static uint8_t clickfinger(const struct m1tp_mt_state *s)
{
    int first = -1, second = -1;
    unsigned i, n = 0;

    for (i = 0; i < M1TP_MT_SLOTS; i++) {
        if (!s->slot[i].used || !s->slot[i].down) continue;
        if (thumb_ignored(s, i) || s->slot[i].palm) continue;
        n++;
        if (first < 0) first = (int)i; else if (second < 0) second = (int)i;
    }
    if (n == 2) {
        const struct m1tp_mt_slot *a = &s->slot[first], *b = &s->slot[second];
        int64_t x = (int64_t)iabs(a->x - b->x) * 1000 / s->cfg.res_x;
        int64_t y = (int64_t)iabs(a->y - b->y) * 1000 / s->cfg.res_y;
        int within = 0;
        /* "maximum horiz spread is 40mm horiz, 30mm vert" */
        if (x <= 40000 && y <= 30000) {
            within = 1;
            /* 20 to 30 mm apart vertically on a pad taller than 50 mm: not
             * together if exactly one of them is in the bottom 20 mm */
            if (y > 20000 && (s->cfg.max_y - s->cfg.min_y) / s->cfg.res_y >= 50) {
                int32_t bottom = -s->cfg.min_y - 20 * s->cfg.res_y;
                if ((a->y > bottom) != (b->y > bottom)) within = 0;
            }
        }
        n = within ? 2 : 1;
    }
    if (n < 1) n = 1;
    /* LEFT, RIGHT, MIDDLE; four or more: no button */
    return n == 1 ? 1 : n == 2 ? 2 : n == 3 ? 4 : 0;
}

static int finish(struct m1tp_mt_state *s, uint8_t buttons, int32_t dx, int32_t dy,
                  int32_t wheel, struct m1tp_mt_out *out)
{
    int32_t cx = dx > 32767 ? 32767 : dx < -32767 ? -32767 : dx;
    int32_t cy = dy > 32767 ? 32767 : dy < -32767 ? -32767 : dy;
    out->buttons = buttons; out->dx = (int16_t)cx; out->dy = (int16_t)cy;
    out->wheel = (int8_t)(wheel > 127 ? 127 : wheel < -127 ? -127 : wheel);
    if (buttons == s->emitted_buttons && !out->dx && !out->dy && !out->wheel)
        return 0;
    s->emitted_buttons = buttons;
    return 1;
}

int m1tp_mt_step(struct m1tp_mt_state *s, const struct m1tp_mt_frame *f, uint64_t host_us,
                 struct m1tp_mt_out *out)
{
    int32_t dx = 0, dy = 0, wheel = 0, sum_x = 0, sum_y = 0;
    unsigned i, c, nactive = 0, newest = THUMB_NONE;
    uint64_t now;
    int new_touch = 0, pressed;

    if (!s || !f || !out) return 0;
    memset(out, 0, sizeof(*out));
    out->scale_q16 = 65536;
    if (f->kind == M1TP_MT_KIND_RESET) {
        if (s->pinch_active) pinch_end(s,out,M1TP_MT_GESTURE_CANCEL);
        s->gesture = M1TP_MT_G_NONE;s->finger_count = 0;s->finger_pending = 0;
        return 0;
    }
    if (f->kind == M1TP_MT_KIND_BASIC) {
        if (s->pinch_active) pinch_end(s,out,M1TP_MT_GESTURE_CANCEL);
        /* The controller is (still) in its basic mode: its report is the result. */
        for (i = 0; i < M1TP_MT_SLOTS; i++) { touch_end(s, i); s->slot[i].used = 0; }
        s->gesture = M1TP_MT_G_NONE; s->finger_count = 0;
        s->button_down = 0; s->button_mask = 0;
        return finish(s, (uint8_t)(f->mouse_buttons & 7u), f->mouse_dx, f->mouse_dy, 0, out);
    }
    if (f->kind != M1TP_MT_KIND_TOUCH) return 0;

    /* Time. Reports are read by a 2 ms poll, so their arrival times jitter by
     * a quarter of the 8 ms report interval; the controller's own millisecond
     * counter does not. Use its difference whenever it agrees with the host
     * clock to within 10 ms, the host clock otherwise. */
    now = host_us;
    if (s->have_clock) {
        uint64_t host_delta = host_us >= s->last_report_us ? host_us - s->last_report_us : 0;
        uint64_t dev_delta = (uint64_t)(uint16_t)(f->timestamp_ms - s->clock_ms) * 1000;
        if (dev_delta >= 1000 && dev_delta <= 50000 && host_delta < dev_delta + 10000 &&
            dev_delta < host_delta + 10000) {
            now = s->now_us + dev_delta;
            s->events |= M1TP_MT_EV_DEVICE_CLOCK;
        } else if (host_us <= s->now_us) {
            now = s->now_us + 1;
        }
    }
    s->have_clock = 1; s->clock_ms = f->timestamp_ms; s->last_report_us = host_us; s->now_us = now;
    dwt_expire(s, now);

    /* Give each record its slot: the nearest one of the previous report. Linux
     * does this in the kernel (input_mt_assign_slots() in magicmouse_raw_event_mtp()). */
    for (i = 0; i < M1TP_MT_SLOTS; i++) s->slot[i].seen = 0;
    for (c = 0; c < f->contacts; c++) {
        int best = -1, spare = -1;
        int64_t best_d = 0;
        int32_t x = f->c[c].x, y = -(int32_t)f->c[c].y;
        struct m1tp_mt_slot *t;
        uint32_t major = (uint32_t)(f->c[c].touch_major > 0 ? f->c[c].touch_major : 0) * 2;
        uint32_t minor = (uint32_t)(f->c[c].touch_minor > 0 ? f->c[c].touch_minor : 0) * 2;

        for (i = 0; i < M1TP_MT_SLOTS; i++) {
            int64_t d;
            if (!s->slot[i].used) { if (spare < 0) spare = (int)i; continue; }
            if (s->slot[i].seen) continue;
            d = (int64_t)(x - s->slot[i].x) * (x - s->slot[i].x) +
                (int64_t)(y - s->slot[i].y) * (y - s->slot[i].y);
            if (best < 0 || d < best_d) { best = (int)i; best_d = d; }
        }
        if (best < 0) {
            if (spare < 0) continue;                /* more fingers than slots */
            best = spare;
            memset(&s->slot[best], 0, sizeof(s->slot[best]));
            s->slot[best].used = 1;
        }
        t = &s->slot[best];
        t->seen = 1;
        t->prev_x = t->x; t->prev_y = t->y; t->x = x; t->y = y; t->dx = t->dy = 0;

        /* tp_unhover_size() with AttrTouchSizeRange (ABS_MT_TOUCH_MAJOR is
         * twice the record's touch_major: report_finger_data()) */
        if (!t->down) {
            if (!((major > s->cfg.touch_high && minor > s->cfg.touch_low) ||
                  (major > s->cfg.touch_low && minor > s->cfg.touch_high)))
                continue;
            /* tp_begin_touch() */
            t->down = 1; t->begin_us = now; t->history = 0; t->exceeded = 0; t->speed = 0;
            t->pinned = 0; t->jump_last = 0; t->palm = 0;
            t->start_x = x; t->start_y = y;
            s->fingers_down++;
            new_touch = 1; newest = (unsigned)best;
            /* tp_palm_detect_dwt_triggered() */
            if (s->cfg.dwt && s->keyboard_active) { t->palm = 2; s->events |= M1TP_MT_EV_PALM; }
        } else if (major < s->cfg.touch_low || minor < s->cfg.touch_low) {
            touch_end(s, (unsigned)best);
            continue;
        } else if (t->history) {
            /* tp_detect_jumps(): more than 20 mm in a 12 ms interval, or 7 mm
             * more than in the previous one */
            uint64_t tdelta = now - t->prev_us;
            if (tdelta && tdelta <= 30000) {
                int64_t um = travel_um(s, x - t->prev_x, y - t->prev_y) * 12000 / (int64_t)tdelta;
                int jump = um > 20000 || um - t->jump_last > 7000;
                t->jump_last = (int32_t)(um > 0x7fffffff ? 0x7fffffff : um);
                if (jump) t->history = 0;           /* tp_motion_history_reset() */
            }
        }
        thumb_update_touch(s, (unsigned)best, t->history == 0 && t->begin_us == now);
        /* tp_palm_detect_touch_size_triggered(): sticky until the touch ends */
        if (t->palm != 2 && (major > s->cfg.palm_size || minor > s->cfg.palm_size)) {
            t->palm = 1; s->events |= M1TP_MT_EV_PALM;
        }
        /* a touch that began while typing is released when typing stops */
        if (t->palm == 2 && !s->keyboard_active) t->palm = 0;
        if (t->history) { t->dx = x - t->prev_x; t->dy = y - t->prev_y; }
        /* "count each event that we're over the threshold up to 10 events"
         * (THUMB_IGNORE_SPEED_THRESHOLD, 20 mm/s), then tp_calculate_motion_speed() */
        if (t->speed > 20000) { if (t->exceeded < 15) t->exceeded++; }
        else if (t->exceeded) t->exceeded--;
        if (t->history < 4) t->history++;
        if (t->history >= 4 && now > t->prev_us)
            t->speed = (int32_t)(travel_um(s, x - t->prev_x, y - t->prev_y) * 1000000 /
                                 (int64_t)(now - t->prev_us));
        t->prev_us = now;
        /* tp_unpin_finger(): "1.5mm movement -> unpin" */
        if (t->pinned && travel_um(s, x - t->pin_x, y - t->pin_y) >= 1500) t->pinned = 0;
    }
    for (i = 0; i < M1TP_MT_SLOTS; i++)
        if (s->slot[i].used && !s->slot[i].seen) { touch_end(s, i); s->slot[i].used = 0; }
    if (!s->fingers_down) s->pinch_eligible = 1;    /* tp_thumb_reset() once all are up */

    if (new_touch) {
        if (s->cfg.thumb && s->fingers_down >= 2) thumb_update_multifinger(s, newest);
        s->accel_last_us = now;                     /* filter_restart() */
    }

    /* The physical button (tp_header.buttons bit 0, as hid-magicmouse.c reads
     * it). libinput's clickfinger decides which button at the press. */
    pressed = (f->tp_buttons & 1u) != 0;
    if (pressed && !s->button_down) {
        s->button_down = 1;
        s->button_mask = clickfinger(s);
        if (s->button_mask == 2) s->events |= M1TP_MT_EV_RIGHT_CLICK;
        /* tp_pin_fingers(): a click does not move the pointer */
        for (i = 0; i < M1TP_MT_SLOTS; i++)
            if (s->slot[i].used) { s->slot[i].pinned = 1; s->slot[i].pin_x = s->slot[i].x; s->slot[i].pin_y = s->slot[i].y; }
    } else if (!pressed && s->button_down) {
        s->button_down = 0; s->button_mask = 0;
    }

    if (f->mouse_dx || f->mouse_dy) {
        s->events |= M1TP_MT_EV_HEADER_MOTION;
        if (s->cfg.use_header_motion) s->header_motion = 1;
    }

    /* tp_gesture_update_finger_state() */
    for (i = 0; i < M1TP_MT_SLOTS; i++) {
        if (!active_for_gesture(s, i)) continue;
        nactive++; sum_x += s->slot[i].dx; sum_y += s->slot[i].dy;
    }
    if (nactive != s->finger_count) {
        if (!nactive) {
            s->gesture = M1TP_MT_G_NONE; s->finger_count = 0; s->finger_pending = 0;
        } else if (s->gesture != M1TP_MT_G_SCROLL && s->gesture != M1TP_MT_G_SWIPE && s->gesture != M1TP_MT_G_PINCH) {
            gesture_begin(s, nactive, now);
        } else if (nactive != s->finger_pending) {
            /* DEFAULT_GESTURE_SWITCH_TIMEOUT: a scroll survives 100 ms of a
             * different finger count */
            s->finger_pending = (uint8_t)nactive; s->pending_since_us = now;
        } else if (now - s->pending_since_us >= 100000) {
            gesture_begin(s, nactive, now);
        }
    } else {
        s->finger_pending = 0;
    }

    /* tp_gesture_post_events(): "if the physical button is down, don't allow
     * gestures unless the button is held down by a *thumb*" */
    if (nactive && s->button_down && s->thumb_state == M1TP_MT_T_FINGER &&
        s->gesture != M1TP_MT_G_POINTER) {
        s->gesture = M1TP_MT_G_POINTER; s->finger_count = 1; s->finger_pending = 0;
    }
    if (s->gesture == M1TP_MT_G_UNKNOWN) gesture_detect(s, now);

    /* Three- and four-finger swipes (libinput's swipe gesture, tp_gesture_post_gesture()):
     * the average travel of the active touches, accelerated as the pointer is, in libinput's
     * units. A finger lifted or added is debounced as for scrolling (above); the swipe ends
     * when every finger has left or the debounced count is another. Physical directions:
     * natural scrolling does not apply. */
    if (s->swipe_fingers && (s->gesture != M1TP_MT_G_SWIPE || !nactive || s->finger_count != s->swipe_fingers)) {
        out->swipe = M1TP_MT_SWIPE_END; out->swipe_fingers = s->swipe_fingers;
        s->swipe_fingers = 0;
    } else if (s->gesture == M1TP_MT_G_SWIPE && nactive && !out->swipe && !s->pinch_active) {
        if (!s->swipe_fingers) {
            s->swipe_fingers = s->finger_count; s->swipe_rem_x = s->swipe_rem_y = 0;
            s->accel_last_us = now;                 /* filter_restart() */
            out->swipe = M1TP_MT_SWIPE_BEGIN; out->swipe_fingers = s->swipe_fingers;
        } else {
            int32_t sx, sy;
            accelerate_gain(s, sum_x / (int32_t)nactive, sum_y / (int32_t)nactive, now, 100,
                            &s->swipe_rem_x, &s->swipe_rem_y, &sx, &sy);
            if (sx || sy) {
                out->swipe = M1TP_MT_SWIPE_UPDATE; out->swipe_fingers = s->swipe_fingers;
                out->swipe_dx = (int16_t)(sx > 32767 ? 32767 : sx < -32767 ? -32767 : sx);
                out->swipe_dy = (int16_t)(sy > 32767 ? 32767 : sy < -32767 ? -32767 : sy);
            }
        }
    }

    pinch_step(s,nactive,now,out);

    if (s->gesture == M1TP_MT_G_POINTER && nactive) {
        if (s->header_motion) { dx = f->mouse_dx; dy = f->mouse_dy; }
        else {
            /* tp_get_raw_pointer_motion(): "When a clickpad is clicked, combine
             * motion of all active touches", else their average */
            int32_t ux = sum_x, uy = sum_y;
            if (!s->button_down) { ux /= (int32_t)nactive; uy /= (int32_t)nactive; }
            accelerate(s, ux, uy, now, &dx, &dy);
        }
    } else if (s->gesture == M1TP_MT_G_SCROLL && nactive) {
        /* tp_gesture_handle_state_scroll(): the average travel, not
         * accelerated; evdev_post_scroll() with the touchpad's threshold of 0.
         * Vertical only: the pointer record has one wheel byte. */
        const int64_t step = (int64_t)s->cfg.scroll_step_px << 16;
        s->rem_scroll += (int64_t)sum_y * s->scroll_per_unit / (int64_t)nactive;
        wheel = (int32_t)(s->rem_scroll / step);
        s->rem_scroll -= (int64_t)wheel * step;
        /* Fingers moving towards the user scroll down (wheel towards the
         * user, negative here); natural scrolling inverts that. */
        if (!s->cfg.natural_scroll) wheel = -wheel;
        if (wheel) s->events |= M1TP_MT_EV_SCROLL;
    }
    return finish(s, s->button_mask, dx, dy, wheel, out);
}

size_t m1tp_mt_build(uint8_t *report, size_t capacity, uint8_t tp_buttons,
    uint16_t timestamp_ms, unsigned contacts, const int16_t (*xy)[2], const int16_t *major)
{
    const size_t prefix = M1TP_MT_MOUSE_BYTES + M1TP_MT_HEADER_BYTES;
    size_t size = prefix + (size_t)contacts * M1TP_MT_FINGER_BYTES;
    unsigned i;

    if (!report || contacts > M1TP_MT_MAX_CONTACTS || size > capacity || (contacts && !xy))
        return 0;
    memset(report, 0, size);
    report[0] = M1TP_MT_REPORT_ID;
    report[M1TP_MT_MOUSE_BYTES + TP_HDR_TIMESTAMP] = (uint8_t)timestamp_ms;
    report[M1TP_MT_MOUSE_BYTES + TP_HDR_TIMESTAMP + 1] = (uint8_t)(timestamp_ms >> 8);
    report[M1TP_MT_MOUSE_BYTES + TP_HDR_NUM_FINGERS] = (uint8_t)contacts;
    report[M1TP_MT_MOUSE_BYTES + TP_HDR_BUTTONS] = tp_buttons;
    for (i = 0; i < contacts; i++) {
        uint8_t *r = report + prefix + (size_t)i * M1TP_MT_FINGER_BYTES;
        int16_t m = major ? major[i] : 170;
        put16(r + TP_F_ABS_X, xy[i][0]);
        put16(r + TP_F_ABS_Y, xy[i][1]);
        put16(r + TP_F_TOUCH_MAJOR, m);
        put16(r + TP_F_TOUCH_MINOR, (int16_t)(m * 8 / 5));
        put16(r + TP_F_PRESSURE, 60);
    }
    return size;
}
