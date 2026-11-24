// SPDX-License-Identifier: GPL-2.0-or-later
/* Host tests of the multitouch decoder. Fixtures are constructed from the
 * report layout in the pinned hid-magicmouse.c, in physical units, plus one
 * stroke recorded on the trackpad (boot mt01). The pointer and scroll results
 * are checked against libinput 1.32.0's formulas evaluated here in floating
 * point (ref_*), independently of the integer code under test. */
#include "m1tp_mt.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned m1tp_mt_recorded(unsigned, int16_t *, int16_t *, int16_t *, int16_t *, unsigned *);

static uint8_t buf[M1TP_MT_MAX_REPORT_BYTES + 64];
static struct m1tp_mt_state st;
static struct m1tp_mt_frame fr;
static struct m1tp_mt_out out;
static uint64_t now_us = 1000000;

/* ---- libinput 1.32.0 in floating point --------------------------------- */
#define RES_X 98.0
#define RES_Y 97.0
#define DPI 2489.0              /* (int)(98 * 25.4), tp_init() */
#define GAIN 2.0                /* monitor scale 2 on the reference desktop */
/* filter-touchpad.c touchpad_accel_profile_linear(), threshold 130, speed 0 */
static double ref_profile(double units_per_us)
{
    double speed = units_per_us * 1000000.0 * 25.4 / DPI, factor;
    if (speed < 7.0) factor = fmin(0.9, 0.1 * speed + 0.3);
    else if (speed < 130.0) factor = 0.9;
    else { speed = fmin(speed, 520.0); factor = 0.0025 * (speed / 130.0) * (speed - 130.0) + 0.9; }
    return factor * 0.2968;
}
struct ref { double last_v, last_t, x, y; };
/* accelerator_filter_touchpad(): trackers with 2 entries, Simpson, normalize */
static void ref_motion(struct ref *r, double dx_units, double dy_units, double t_us)
{
    double x = dx_units, y = dy_units * RES_X / RES_Y, tdelta, v, f;
    if (x == 0 && y == 0) return;
    tdelta = t_us - r->last_t; if (tdelta > 1000000.0) tdelta = 1000000.0;
    r->last_t = t_us;
    v = hypot(x, y) / (tdelta + 1.0);
    f = (ref_profile(v) + ref_profile(r->last_v) + 4.0 * ref_profile((v + r->last_v) / 2.0)) / 6.0;
    r->last_v = v;
    r->x += x * f * 1000.0 / DPI * GAIN; r->y += y * f * 1000.0 / DPI * GAIN;
}
/* One finger along x at a constant speed in 8 ms reports, with the same
 * integer positions the fixture builder produces. */
static double ref_stroke(double mm, double speed)
{
    struct ref r = { 0, 0, 0, 0 };
    int frames = (int)(mm * 1000 * 125 / (speed * 1000)), i;
    int start = -6046 + (int)(20000LL * 98 / 1000), prev = start;
    double t = 32000;   /* landed for four reports; filter_restart() at the first */
    r.last_t = 8000;
    for (i = 1; i <= frames; i++) {
        int um = 20000 + (int)((long long)(mm * 1000) * i / frames);
        int x = -6046 + (int)((long long)um * 98 / 1000);
        t += 8000; ref_motion(&r, x - prev, 0, t); prev = x;
    }
    return r.x;
}

static int feed(unsigned ms, uint8_t button, unsigned n, const int16_t (*xy)[2], const int16_t *major)
{
    size_t size;
    now_us += (uint64_t)ms * 1000;
    size = m1tp_mt_build(buf, sizeof(buf), button, (uint16_t)(now_us / 1000), n, xy, major);
    assert(size == 46 + 30 * n);
    assert(m1tp_mt_parse(buf, size, &fr) == M1TP_MT_KIND_TOUCH);
    return m1tp_mt_step(&st, &fr, now_us, &out);
}
static unsigned sunk; static struct m1tp_mt_out last;
static void sink(void *context, const struct m1tp_mt_out *o) { assert(context == &sunk); sunk++; last = *o; }
static void close_to(const char *what, double got, double want, double tolerance)
{
    printf("  %-34s got %9.1f  libinput %9.1f  (%+.2f %%)\n", what, got, want,
           want ? (got - want) * 100.0 / want : 0.0);
    if (fabs(got - want) > tolerance) { printf("FAIL: %s\n", what); exit(1); }
}

int main(void)
{
    unsigned checks = 0, reports = 0, failed, i;
    struct m1tp_mt_measured m;
    size_t size;

    /* layout constants against the Linux structures */
    assert(M1TP_MT_MOUSE_BYTES + M1TP_MT_HEADER_BYTES == 46);
    assert(M1TP_MT_FINGER_BYTES == 30 && M1TP_MT_MAX_REPORT_BYTES == 526);
    assert(m1tp_mt_enable_payload[0] == 0x02 && m1tp_mt_enable_payload[1] == 0x01);
    assert(m1tp_mt_pointer_descriptor[0] == 0x05 && m1tp_mt_pointer_descriptor[M1TP_MT_POINTER_DESCRIPTOR_BYTES - 1] == 0xc0);

    /* geometry: what Linux reports for this pad (128 x 78 mm) */
    m1tp_mt_init(&st, 0);
    assert((st.cfg.max_x - st.cfg.min_x) / st.cfg.res_x == 128);
    assert((st.cfg.max_y - st.cfg.min_y) / st.cfg.res_y == 78 && st.dpi == 2489);

    /* the acceleration profile against libinput's, across the speed range */
    for (i = 0; i <= 700; i++) {
        double mm_s = i, want = ref_profile(mm_s * DPI / 25.4 / 1000000.0);
        double got = m1tp_mt_accel_factor(&st, (int64_t)(mm_s * 1000)) / 65536.0;
        assert(fabs(got - want) < 0.0005);
    }
    printf("acceleration factor (libinput speed 0): 5 mm/s %.4f, 50 mm/s %.4f, 200 mm/s %.4f, 400 mm/s %.4f, >=520 mm/s %.4f\n",
           m1tp_mt_accel_factor(&st, 5000) / 65536.0, m1tp_mt_accel_factor(&st, 50000) / 65536.0,
           m1tp_mt_accel_factor(&st, 200000) / 65536.0, m1tp_mt_accel_factor(&st, 400000) / 65536.0,
           m1tp_mt_accel_factor(&st, 600000) / 65536.0);

    /* the shared scenarios */
    failed = m1tp_mt_selftest(&st, buf, sizeof(buf), &checks, &reports, &m, sink, &sunk);
    printf("scenarios: failed_check=%u checks=%u reports=%u delivered=%u\n", failed, checks, reports, sunk);
    printf("measured: slow %d counts / 20 mm, medium %d / 40 mm, fast %d / 80 mm, recorded stroke %d/%d, scroll %d steps / 50 mm\n",
           m.slow_counts, m.medium_counts, m.fast_counts, m.recorded_dx, m.recorded_dy, m.scroll_steps);
    if (failed) printf("failing check: pointer travel %d\n", m.failed_dx);
    assert(failed == 0 && sunk > 0 && last.buttons == 0);
    assert(m1tp_mt_selftest(&st, buf, 100, &checks, &reports, 0, 0, 0) == 1 && checks == 0);

    /* ... and their totals against libinput's arithmetic */
    printf("pointer travel in counts (one count = one panel pixel = half a libinput unit):\n");
    close_to("10 mm/s for 20 mm", m.slow_counts, ref_stroke(20, 10), 2.0);
    close_to("100 mm/s for 40 mm", m.medium_counts, ref_stroke(40, 100), 2.0);
    close_to("400 mm/s for 80 mm", m.fast_counts, ref_stroke(80, 400), 6.0);
    {
        struct ref r = { 0, 0, 0, 0 };
        int16_t x, y, major, minor, px = 0, py = 0;
        unsigned ms, n = m1tp_mt_recorded(0, &x, &y, &major, &minor, &ms), down = 0;
        double t = 0;
        for (i = 0; i < n; i++) {
            m1tp_mt_recorded(i, &x, &y, &major, &minor, &ms);
            t += ms * 1000.0;
            /* tp_unhover_size() with AttrTouchSizeRange=150:130 on doubled sizes */
            if (!down) {
                if ((2 * major > 150 && 2 * minor > 130) || (2 * major > 130 && 2 * minor > 150)) {
                    down = 1; r.last_t = t;
                }
            } else if (2 * major < 130 || 2 * minor < 130) {
                down = 0;
            } else {
                ref_motion(&r, x - px, -(y - py), t);
            }
            px = x; py = y;
        }
        close_to("recorded stroke, x", m.recorded_dx, r.x, 2.0);
        close_to("recorded stroke, y", m.recorded_dy, r.y, 2.0);
    }
    {
        /* tp_filter_scroll(): normalized travel * 0.9 * 0.2968, Hyprland
         * scroll_factor 0.4, 15 units per detent; the first 1.5 mm decide the
         * gesture and are not scrolled (tp_gesture_detect_motion_gestures()) */
        double per_mm = 1000.0 / 25.4 * 0.9 * 0.2968 * 0.4, mm_per_step = 15.0 / per_mm;
        printf("scroll: %.3f mm of two-finger travel per wheel step (%.1f contact units)\n",
               mm_per_step, mm_per_step * RES_Y);
        /* fingers moving away from the user, natural scrolling: wheel towards the user */
        close_to("two fingers, 50 mm away", m.scroll_steps, -floor((50.0 - 1.5) / mm_per_step), 1.0);
    }

    /* parse: sizes and counts the Linux driver refuses */
    const int16_t two[2][2] = { { 100, 200 }, { -300, 400 } };
    size = m1tp_mt_build(buf, sizeof(buf), 0, 0x1234, 2, two, 0);
    assert(size == 106);
    assert(m1tp_mt_parse(buf, size, &fr) == M1TP_MT_KIND_TOUCH && fr.contacts == 2);
    assert(fr.c[1].x == -300 && fr.c[1].y == 400 && fr.c[0].touch_major == 170 && fr.timestamp_ms == 0x1234);
    assert(m1tp_mt_parse(buf, size - 1, &fr) == M1TP_MT_KIND_INVALID);
    assert(m1tp_mt_parse(buf, 45, &fr) == M1TP_MT_KIND_INVALID);
    assert(m1tp_mt_parse(buf, 9, &fr) == M1TP_MT_KIND_INVALID);
    assert(m1tp_mt_parse(buf, 7, &fr) == M1TP_MT_KIND_INVALID);
    assert(m1tp_mt_parse(buf, 0, &fr) == M1TP_MT_KIND_INVALID);
    assert(m1tp_mt_parse(0, 8, &fr) == M1TP_MT_KIND_INVALID);
    assert(m1tp_mt_parse(buf, 8, &fr) == M1TP_MT_KIND_BASIC);
    buf[8 + 22] = 3;  /* three fingers announced, two records present */
    assert(m1tp_mt_parse(buf, size, &fr) == M1TP_MT_KIND_INVALID);
    buf[8 + 22] = 1;
    assert(m1tp_mt_parse(buf, size, &fr) == M1TP_MT_KIND_TOUCH && fr.contacts == 1 && fr.reported == 1);
    buf[0] = 0x3f;
    assert(m1tp_mt_parse(buf, size, &fr) == M1TP_MT_KIND_INVALID);
    buf[0] = 0x60;
    assert(m1tp_mt_parse(buf, 2, &fr) == M1TP_MT_KIND_RESET);
    assert(m1tp_mt_build(buf, sizeof(buf), 0, 0, 17, two, 0) == 0);
    memset(buf, 0, sizeof(buf)); buf[0] = 2;
    assert(m1tp_mt_parse(buf, 46 + 30 * 17, &fr) == M1TP_MT_KIND_INVALID);
    assert(m1tp_mt_parse(buf, 46 + 30 * 16, &fr) == M1TP_MT_KIND_TOUCH && fr.contacts == 0);

    /* basic-mode reports pass through; the out-of-range idle value is not motion */
    memset(buf, 0, 8); buf[0] = 2; buf[2] = 0x80; buf[3] = 0x80;
    m1tp_mt_init(&st, 0);
    assert(m1tp_mt_parse(buf, 8, &fr) == M1TP_MT_KIND_BASIC && !fr.mouse_dx && !fr.mouse_dy);
    assert(m1tp_mt_step(&st, &fr, now_us, &out) == 0);
    buf[2] = 0xcc; buf[3] = 0x1a;  /* bytes of a real basic-mode report (boot pr02) */
    assert(m1tp_mt_parse(buf, 8, &fr) == M1TP_MT_KIND_BASIC);
    assert(m1tp_mt_step(&st, &fr, now_us, &out) == 1 && out.dx == -52 && out.dy == 26 && out.buttons == 0);

    /* The controller's clock, not the poll's arrival time, sets the speed: the
     * same stroke with +-2 ms of arrival jitter gives the same pointer travel. */
    {
        long total[2] = { 0, 0 };
        int pass, k;
        for (pass = 0; pass < 2; pass++) {
            uint64_t device_us = 5000000;
            int16_t xy[1][2] = { { 0, 3000 } };
            m1tp_mt_init(&st, 0);
            for (k = 0; k < 60; k++) {
                uint64_t host = device_us + (pass ? (uint64_t)((k * 7) % 5) * 1000 : 0);
                device_us += 8000;
                if (k >= 4) xy[0][0] = (int16_t)(xy[0][0] + 157);    /* 200 mm/s */
                size = m1tp_mt_build(buf, sizeof(buf), 0, (uint16_t)(device_us / 1000), 1, xy, 0);
                assert(m1tp_mt_parse(buf, size, &fr) == M1TP_MT_KIND_TOUCH);
                if (m1tp_mt_step(&st, &fr, host, &out)) total[pass] += out.dx;
            }
            assert(st.events & M1TP_MT_EV_DEVICE_CLOCK);
        }
        printf("arrival jitter: %ld counts without, %ld with +-2 ms\n", total[0], total[1]);
        assert(total[0] == total[1] && total[0] > 0);
    }

    /* clickfinger, 20 to 30 mm apart vertically: together unless exactly one
     * of the two is in the pad's bottom 20 mm */
    {
        /* x in units, y as the controller sends it (7439 top, -164 bottom) */
        const int16_t upper[2][2] = { { 0, 6000 }, { 500, 6000 - 25 * 97 } };
        const int16_t lower[2][2] = { { 0, 3800 }, { 500, 3800 - 25 * 97 } };  /* second 14 mm from the bottom */
        m1tp_mt_init(&st, 0);
        for (i = 0; i < 4; i++) assert(feed(8, 0, 2, upper, 0) == 0);
        assert(feed(8, 1, 2, upper, 0) == 1 && out.buttons == 2);
        assert(feed(8, 0, 2, upper, 0) == 1 && out.buttons == 0);
        for (i = 0; i < 3; i++) feed(8, 0, 0, 0, 0);
        m1tp_mt_init(&st, 0);
        for (i = 0; i < 4; i++) assert(feed(8, 0, 2, lower, 0) == 0);
        assert(feed(8, 1, 2, lower, 0) == 1 && out.buttons == 1);
        assert(feed(8, 0, 2, lower, 0) == 1 && out.buttons == 0);
    }
    /* four fingers: libinput sends no button */
    {
        const int16_t four[4][2] = { { 0, 4000 }, { 1500, 4000 }, { 3000, 4000 }, { 4500, 4000 } };
        m1tp_mt_init(&st, 0);
        for (i = 0; i < 4; i++) assert(feed(8, 0, 4, four, 0) == 0);
        assert(feed(8, 1, 4, four, 0) == 0 && feed(8, 0, 4, four, 0) == 0);
    }
    /* the button chosen at the press holds until the release, whatever lifts */
    {
        const int16_t pair[2][2] = { { 1000, 3000 }, { 2000, 3000 } };
        const int16_t one[1][2] = { { 1000, 3000 } };
        m1tp_mt_init(&st, 0);
        for (i = 0; i < 4; i++) assert(feed(8, 0, 2, pair, 0) == 0);
        assert(feed(8, 1, 2, pair, 0) == 1 && out.buttons == 2);
        assert(feed(8, 1, 1, one, 0) == 0);
        assert(feed(8, 0, 1, one, 0) == 1 && out.buttons == 0);
    }
    /* a pinch has a separate gesture event, never pointer/wheel bytes */
    {
        int16_t a[2][2] = { { 0, 4000 }, { 2000, 4000 } };
        long moved = 0;
        m1tp_mt_init(&st, 0);
        for (i = 0; i < 4; i++) feed(8, 0, 2, a, 0);
        for (i = 0; i < 40; i++) { a[0][0] -= 30; a[1][0] += 30; if (feed(8, 0, 2, a, 0)) moved++; }
        assert(!moved && st.gesture == M1TP_MT_G_PINCH);
        for (i = 0; i < 3; i++) feed(8, 0, 0, 0, 0);
        a[0][0] = 0; a[1][0] = 2000;
        for (i = 0; i < 4; i++) feed(8, 0, 2, a, 0);
        for (i = 0; i < 40; i++) { a[0][0] += 40; a[1][0] += 40; if (feed(8, 0, 2, a, 0)) moved++; }
        assert(!moved && st.gesture == M1TP_MT_G_SCROLL);   /* horizontal: no wheel byte for it */
    }
    /* disable-while-typing from keyboard reports: letters trigger it, a
     * Command chord and Escape do not, a held key keeps it going */
    {
        uint8_t prev[6] = { 0 };
        uint8_t key[10] = { 1, 0, 0, 0x04, 0, 0, 0, 0, 0, 0 };     /* 'a' */
        uint8_t up[10] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8_t chord[10] = { 1, 0x08, 0, 0x06, 0, 0, 0, 0, 0, 0 }; /* Cmd+c */
        uint8_t esc[10] = { 1, 0, 0, 0x29, 0, 0, 0, 0, 0, 0 };
        const int16_t one[1][2] = { { 1000, 3000 } };
        m1tp_mt_init(&st, 0);
        m1tp_mt_keyboard_report(&st, now_us, chord, sizeof(chord), prev);
        assert(!st.keyboard_active);
        m1tp_mt_keyboard_report(&st, now_us, up, sizeof(up), prev);
        m1tp_mt_keyboard_report(&st, now_us, esc, sizeof(esc), prev);
        assert(!st.keyboard_active);
        m1tp_mt_keyboard_report(&st, now_us, up, sizeof(up), prev);
        m1tp_mt_keyboard_report(&st, now_us, key, sizeof(key), prev);
        assert(st.keyboard_active);
        feed(100, 0, 1, one, 0);
        assert(st.keyboard_active && st.slot[0].palm == 2);          /* landed while typing */
        feed(600, 0, 1, one, 0);
        assert(st.keyboard_active);                                  /* the key is still held */
        m1tp_mt_keyboard_report(&st, now_us, up, sizeof(up), prev);
        feed(600, 0, 1, one, 0);
        assert(!st.keyboard_active && st.slot[0].palm == 0);
        /* off by configuration */
        struct m1tp_mt_config cfg; memset(&cfg, 0, sizeof(cfg)); cfg.dwt = M1TP_MT_OFF;
        m1tp_mt_init(&st, &cfg);
        m1tp_mt_keyboard_report(&st, now_us, key, sizeof(key), prev);
        m1tp_mt_keyboard_report(&st, now_us, up, sizeof(up), prev);
        m1tp_mt_keyboard_report(&st, now_us, key, sizeof(key), prev);
        assert(!st.keyboard_active);
    }
    /* overrides: a flat scale, a scroll distance, the gain */
    {
        struct m1tp_mt_config cfg;
        int16_t xy[1][2] = { { 0, 3000 } };
        int16_t pair[2][2] = { { 0, 3000 }, { 1500, 3000 } };
        long counts = 0, wheel = 0;
        memset(&cfg, 0, sizeof(cfg)); cfg.flat_units_per_count = 4;
        m1tp_mt_init(&st, &cfg);
        for (i = 0; i < 4; i++) feed(8, 0, 1, xy, 0);
        for (i = 0; i < 50; i++) { xy[0][0] += 20; if (feed(8, 0, 1, xy, 0)) counts += out.dx; }
        assert(counts == 250);
        memset(&cfg, 0, sizeof(cfg)); cfg.scroll_units_per_step = 100; cfg.natural_scroll = M1TP_MT_OFF;
        m1tp_mt_init(&st, &cfg);
        for (i = 0; i < 3; i++) feed(8, 0, 0, 0, 0);
        for (i = 0; i < 4; i++) feed(8, 0, 2, pair, 0);
        for (i = 0; i < 50; i++) { pair[0][1] += 20; pair[1][1] += 20; if (feed(8, 0, 2, pair, 0)) wheel += out.wheel; }
        /* 1000 units away from the user, 100 per step, less the travel that decided the gesture */
        assert(wheel >= 8 && wheel <= 9);
        memset(&cfg, 0, sizeof(cfg)); cfg.gain_percent = 100;
        m1tp_mt_init(&st, &cfg);
        assert(st.cfg.gain_percent == 100 && st.cfg.res_x == 98 && st.cfg.natural_scroll == 1);
    }
    /* encoding */
    {
        uint8_t rep[M1TP_MT_POINTER_REPORT_BYTES];
        struct m1tp_mt_out o = { .buttons = 2, .dx = -300, .dy = 4, .wheel = -1 };
        m1tp_mt_encode(&o, rep);
        assert(rep[0] == 2 && rep[1] == 2 && rep[2] == 0xd4 && rep[3] == 0xfe && rep[4] == 4 &&
               rep[5] == 0 && rep[6] == 0xff);
    }
    /* swipes: three and four fingers 40 mm to the left in 300 ms give BEGIN, UPDATEs
     * leftwards and one END when the fingers leave, one by one (debounced as scrolling is);
     * no pointer motion and no wheel meanwhile; two fingers give no swipe */
    for (unsigned n = 2; n <= 4; n++) {
        int16_t xy[4][2];
        unsigned begins = 0, ends = 0, updates = 0, i, pointer = 0;
        int32_t total_x = 0, total_y = 0;
        m1tp_mt_init(&st, NULL);
        for (i = 0; i <= 40; i++) {
            for (unsigned f = 0; f < n; f++) {
                xy[f][0] = (int16_t)(2000 + f * 1500 - (int)i * 98);   /* 1 mm per report leftwards */
                xy[f][1] = 3000;
            }
            if (feed(8, 0, n, (const int16_t (*)[2])xy, NULL) && (out.dx || out.dy || out.wheel)) pointer++;
            if (out.swipe == M1TP_MT_SWIPE_BEGIN) { begins++; assert(out.swipe_fingers == n); }
            if (out.swipe == M1TP_MT_SWIPE_UPDATE) { updates++; total_x += out.swipe_dx; total_y += out.swipe_dy; }
            if (out.swipe == M1TP_MT_SWIPE_END) ends++;
        }
        for (unsigned left = n; left-- > 0;) {                   /* lifted one per report */
            feed(8, 0, left, (const int16_t (*)[2])xy, NULL);
            if (out.swipe == M1TP_MT_SWIPE_END) { ends++; assert(out.swipe_fingers == n); }
            if (out.swipe == M1TP_MT_SWIPE_BEGIN) begins++;
        }
        printf("  swipe %u fingers: begin %u update %u end %u travel %d,%d pointer reports with motion %u\n",
               n, begins, updates, ends, (int)total_x, (int)total_y, pointer);
        if (n == 2) { assert(begins == 0 && updates == 0 && ends == 0); continue; }
        assert(begins == 1 && ends == 1 && updates > 20 && total_x < -200 && total_y == 0 && pointer == 0);
    }
    printf("m1tp_mt host tests: PASS\n");
    return 0;
}
