// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Fixed scenarios for the multitouch decoder: the same code runs in the host
 * tests and inside the kernel (sfm1tpselftest). Contacts are given in
 * millimetres from the pad's top left corner and in milliseconds, converted
 * with the pad's real resolution and built into reports in the layout of
 * drivers/hid/hid-magicmouse.c (see m1tp_mt.h). One scenario replays a stroke
 * recorded on this trackpad (boot mt01, MT_DBG lines).
 *
 * The expected totals below are what the integer implementation gives; the
 * host test (test_m1tp_mt.c) checks each of them against libinput 1.32.0's
 * formulas evaluated in floating point, so the kernel only has to reproduce
 * them exactly.
 */
#include "m1tp_mt.h"
#include <string.h>

struct run {
    struct m1tp_mt_state *state;
    uint8_t *buffer; size_t capacity;
    uint64_t now_us;                /* host clock; the report timestamp follows it */
    int32_t sum_dx, sum_dy, sum_wheel;
    unsigned reports, left, right, middle, failed;
    uint8_t buttons;
    m1tp_mt_sink sink; void *context;
};
struct finger { int32_t x_um, y_um; int16_t major; };   /* major 0: the default fingertip */

static void restart(struct run *r, const struct m1tp_mt_config *config)
{
    m1tp_mt_init(r->state, config);
    r->sum_dx = r->sum_dy = r->sum_wheel = 0;
    r->reports = r->left = r->right = r->middle = 0;
    r->buttons = 0;
    r->now_us += 3000000;           /* scenarios are seconds apart */
}

/* One report, advance_ms after the previous one. */
static void feed(struct run *r, unsigned advance_ms, int button, unsigned n, const struct finger *f)
{
    /* static: keeps the frame off the kernel stack; callers are serialized */
    static struct m1tp_mt_frame frame;
    const struct m1tp_mt_config *c = &r->state->cfg;
    int16_t xy[4][2], major[4];
    struct m1tp_mt_out out;
    size_t size;
    unsigned i;

    if (n > 4) { r->failed = 1; return; }
    for (i = 0; i < n; i++) {
        xy[i][0] = (int16_t)(c->min_x + (int32_t)((int64_t)f[i].x_um * c->res_x / 1000));
        xy[i][1] = (int16_t)(c->max_y - (int32_t)((int64_t)f[i].y_um * c->res_y / 1000));
        major[i] = f[i].major ? f[i].major : 170;
    }
    r->now_us += (uint64_t)advance_ms * 1000;
    size = m1tp_mt_build(r->buffer, r->capacity, button ? 1 : 0, (uint16_t)(r->now_us / 1000),
                         n, xy, major);
    if (!size || m1tp_mt_parse(r->buffer, size, &frame) != M1TP_MT_KIND_TOUCH) { r->failed = 1; return; }
    if (!m1tp_mt_step(r->state, &frame, r->now_us, &out)) return;
    r->reports++;
    r->sum_dx += out.dx; r->sum_dy += out.dy; r->sum_wheel += out.wheel;
    if ((out.buttons & 1) && !(r->buttons & 1)) r->left++;
    if ((out.buttons & 2) && !(r->buttons & 2)) r->right++;
    if ((out.buttons & 4) && !(r->buttons & 4)) r->middle++;
    r->buttons = out.buttons;
    if (r->sink) r->sink(r->context, &out);
}

/* n fingers travel (dx_um, dy_um) at speed mm/s in 8 ms reports, then stay
 * for `rest` reports. f[] is updated to where they end. */
static void travel(struct run *r, int button, unsigned n, struct finger *f, int32_t dx_um,
                   int32_t dy_um, unsigned speed_mm_s, unsigned rest)
{
    int32_t ax = dx_um < 0 ? -dx_um : dx_um, ay = dy_um < 0 ? -dy_um : dy_um;
    int32_t length = ax > ay ? ax : ay;     /* scenarios move along one axis */
    unsigned frames = (unsigned)((int64_t)length * 125 / ((int64_t)speed_mm_s * 1000)), i, k;
    struct finger start[4], now[4];

    if (!frames) frames = 1;
    for (k = 0; k < n && k < 4; k++) start[k] = f[k];
    for (i = 1; i <= frames; i++) {
        for (k = 0; k < n && k < 4; k++) {
            now[k] = start[k];
            now[k].x_um += (int32_t)((int64_t)dx_um * i / frames);
            now[k].y_um += (int32_t)((int64_t)dy_um * i / frames);
        }
        feed(r, 8, button, n, now);
    }
    for (k = 0; k < n && k < 4; k++) f[k] = now[k];
    for (i = 0; i < rest; i++) feed(r, 8, button, n, f);
}
static void land(struct run *r, unsigned n, const struct finger *f)
{ unsigned i; for (i = 0; i < 4; i++) feed(r, 8, 0, n, f); }
static void lift(struct run *r)
{ unsigned i; for (i = 0; i < 3; i++) feed(r, 8, 0, 0, 0); }

/* Boot mt01, one finger: position and size as logged; intervals rebuilt from
 * each record's velocity field (rel = units per 80 ms), because the log kept
 * no timestamps: 24 ms where log output delayed the poll, 8 ms otherwise. */
static const struct { int16_t x, y, major, minor; uint8_t ms; } recorded[] = {
    { 3448, 1340, 150, 258, 8 },  { 3427, 1351, 130, 253, 16 }, { 3367, 1369, 99, 217, 24 },
    { 3323, 1372, 97, 186, 16 },  { 3266, 1356, 133, 295, 24 }, { 3223, 1340, 153, 301, 24 },
    { 3174, 1354, 157, 312, 24 }, { 3076, 1419, 163, 266, 16 }, { 2919, 1514, 168, 306, 24 },
    { 2741, 1579, 167, 307, 24 }, { 2552, 1570, 168, 294, 24 }, { 2370, 1498, 172, 318, 24 },
    { 2199, 1401, 167, 295, 24 }, { 2140, 1367, 160, 295, 8 },  { 2081, 1334, 151, 274, 8 },
    { 2022, 1301, 126, 238, 8 },  { 1963, 1269, 82, 136, 8 },   { 1904, 1243, 40, 53, 8 },
};
unsigned m1tp_mt_recorded(unsigned index, int16_t *x, int16_t *y, int16_t *major, int16_t *minor,
                          unsigned *ms);
unsigned m1tp_mt_recorded(unsigned index, int16_t *x, int16_t *y, int16_t *major, int16_t *minor,
                          unsigned *ms)
{
    const unsigned count = (unsigned)(sizeof(recorded) / sizeof(recorded[0]));
    if (index < count) {
        *x = recorded[index].x; *y = recorded[index].y; *major = recorded[index].major;
        *minor = recorded[index].minor; *ms = recorded[index].ms;
    }
    return count;
}

/* The expected totals. */
#define GOLD_SLOW 419
#define GOLD_MEDIUM 839
#define GOLD_FAST 5302
#define GOLD_REC_DX (-318)
#define GOLD_REC_DY 15
#define GOLD_SCROLL (-13)
#define GOLD_THUMB_DX 334

#define CHECK(condition) do { number++; if (measured) *measured = m; \
    if (r.failed || !(condition)) { if (measured) measured->failed_dx = r.sum_dx; return number; } } while (0)

unsigned m1tp_mt_selftest(struct m1tp_mt_state *state, uint8_t *buffer, size_t capacity,
    unsigned *checks, unsigned *reports, struct m1tp_mt_measured *measured,
    m1tp_mt_sink sink, void *context)
{
    struct run r;
    struct finger f[4];
    struct m1tp_mt_measured m;
    struct m1tp_mt_config off;
    unsigned number = 0, total = 0, i;

    if (checks) *checks = 0;
    if (reports) *reports = 0;
    memset(&m, 0, sizeof(m));
    if (!state || !buffer || capacity < M1TP_MT_MOUSE_BYTES + M1TP_MT_HEADER_BYTES + 4 * M1TP_MT_FINGER_BYTES)
        return 1;
    memset(&r, 0, sizeof(r));
    r.state = state; r.buffer = buffer; r.capacity = capacity;
    memset(f, 0, sizeof(f));

    /* 1. The delivered scenario: move, click, two-finger click, scroll both ways. */
    restart(&r, 0);
    r.sink = sink; r.context = context;
    f[0].x_um = 40000; f[0].y_um = 30000;
    land(&r, 1, f);
    travel(&r, 0, 1, f, 5000, 0, 50, 2);                /* 5 mm right */
    CHECK(r.sum_dx > 0 && r.sum_dy == 0 && !r.left && !r.right);
    feed(&r, 8, 1, 1, f); feed(&r, 8, 1, 1, f); feed(&r, 8, 0, 1, f);
    CHECK(r.left == 1 && r.right == 0 && r.buttons == 0);
    lift(&r);
    f[0].x_um = 40000; f[0].y_um = 30000; f[1].x_um = 60000; f[1].y_um = 30000;
    land(&r, 2, f);
    feed(&r, 8, 1, 2, f); feed(&r, 8, 1, 2, f); feed(&r, 8, 0, 2, f);
    CHECK(r.left == 1 && r.right == 1 && r.buttons == 0);    /* two fingers: right button */
    lift(&r);
    land(&r, 2, f);
    i = (unsigned)r.sum_dx;
    travel(&r, 0, 2, f, 0, 10000, 60, 2);               /* 10 mm towards the user */
    CHECK(r.sum_wheel == 2 && (unsigned)r.sum_dx == i); /* natural: wheel up, no pointer motion */
    travel(&r, 0, 2, f, 0, -10000, 60, 2);
    CHECK(r.sum_wheel == 0);
    lift(&r);
    CHECK(r.buttons == 0);
    total += r.reports;
    r.sink = 0;

    /* 2-4. One finger at 10, 100 and 400 mm/s: libinput's deceleration, its
     * plateau and its acceleration incline. */
    restart(&r, 0);
    f[0].x_um = 20000; f[0].y_um = 30000;
    land(&r, 1, f); travel(&r, 0, 1, f, 20000, 0, 10, 0); lift(&r);
    m.slow_counts = r.sum_dx; m.slow_um = 20000;
    CHECK(r.sum_dx == GOLD_SLOW && r.sum_dy == 0 && r.sum_wheel == 0);
    total += r.reports;
    restart(&r, 0);
    f[0].x_um = 20000; f[0].y_um = 30000;
    land(&r, 1, f); travel(&r, 0, 1, f, 40000, 0, 100, 0); lift(&r);
    m.medium_counts = r.sum_dx; m.medium_um = 40000;
    CHECK(r.sum_dx == GOLD_MEDIUM && r.sum_dy == 0);
    total += r.reports;
    restart(&r, 0);
    f[0].x_um = 20000; f[0].y_um = 30000;
    land(&r, 1, f); travel(&r, 0, 1, f, 80000, 0, 400, 0); lift(&r);
    m.fast_counts = r.sum_dx; m.fast_um = 80000;
    CHECK(r.sum_dx == GOLD_FAST && r.sum_dy == 0);
    total += r.reports;

    /* 5. The recorded stroke. */
    restart(&r, 0);
    for (i = 0; i < (unsigned)(sizeof(recorded) / sizeof(recorded[0])); i++) {
        static struct m1tp_mt_frame frame;
        struct m1tp_mt_out out;
        int16_t xy[1][2], major[1];
        size_t size;
        xy[0][0] = recorded[i].x; xy[0][1] = recorded[i].y; major[0] = recorded[i].major;
        r.now_us += (uint64_t)recorded[i].ms * 1000;
        size = m1tp_mt_build(buffer, capacity, 0, (uint16_t)(r.now_us / 1000), 1, xy, major);
        /* the builder derives minor from major; put the recorded one in */
        buffer[M1TP_MT_MOUSE_BYTES + M1TP_MT_HEADER_BYTES + 20] = (uint8_t)recorded[i].minor;
        buffer[M1TP_MT_MOUSE_BYTES + M1TP_MT_HEADER_BYTES + 21] = (uint8_t)(recorded[i].minor >> 8);
        if (!size || m1tp_mt_parse(buffer, size, &frame) != M1TP_MT_KIND_TOUCH) return ++number;
        if (m1tp_mt_step(state, &frame, r.now_us, &out)) { r.reports++; r.sum_dx += out.dx; r.sum_dy += out.dy; }
    }
    m.recorded_dx = r.sum_dx; m.recorded_dy = r.sum_dy;
    CHECK(r.sum_dx == GOLD_REC_DX && r.sum_dy == GOLD_REC_DY);
    CHECK(state->fingers_down == 0);                    /* the last record is below the touch size */
    total += r.reports;

    /* 6. Two fingers, 50 mm at 60 mm/s away from the user. */
    restart(&r, 0);
    f[0].x_um = 40000; f[0].y_um = 65000; f[1].x_um = 58000; f[1].y_um = 63000;
    land(&r, 2, f); travel(&r, 0, 2, f, 0, -50000, 60, 0); lift(&r);
    m.scroll_steps = r.sum_wheel; m.scroll_um = 50000;
    CHECK(r.sum_wheel == GOLD_SCROLL && r.sum_dx == 0 && r.sum_dy == 0);
    total += r.reports;
    /* ... and with natural scrolling off the same travel scrolls the other way */
    m1tp_mt_default_config(&off); off.natural_scroll = M1TP_MT_OFF;
    restart(&r, &off);
    f[0].x_um = 40000; f[0].y_um = 65000; f[1].x_um = 58000; f[1].y_um = 63000;
    land(&r, 2, f); travel(&r, 0, 2, f, 0, -50000, 60, 0); lift(&r);
    CHECK(r.sum_wheel == -GOLD_SCROLL);
    total += r.reports;

    /* 7. Clicks: far apart = left; three fingers = middle; a thumb resting at
     * the bottom edge under a finger = left. */
    restart(&r, 0);
    f[0].x_um = 20000; f[0].y_um = 30000; f[1].x_um = 90000; f[1].y_um = 30000;
    land(&r, 2, f); feed(&r, 8, 1, 2, f); feed(&r, 8, 0, 2, f); lift(&r);
    CHECK(r.left == 1 && r.right == 0);
    f[0].x_um = 40000; f[1].x_um = 58000; f[2].x_um = 76000; f[2].y_um = 30000;
    land(&r, 3, f); feed(&r, 8, 1, 3, f); feed(&r, 8, 0, 3, f); lift(&r);
    CHECK(r.left == 1 && r.middle == 1 && r.right == 0);
    f[0].x_um = 60000; f[0].y_um = 76000;               /* the thumb, 2 mm from the bottom edge */
    land(&r, 1, f);
    for (i = 0; i < 20; i++) feed(&r, 8, 0, 1, f);      /* resting: no pointer motion, jailed */
    f[1].x_um = 62000; f[1].y_um = 40000;
    land(&r, 2, f);
    feed(&r, 8, 1, 2, f); feed(&r, 8, 0, 2, f);
    CHECK(r.left == 2 && r.right == 0 && r.sum_dx == 0 && r.sum_dy == 0);
    lift(&r);
    total += r.reports;
    /* 8. ... and the finger above a resting thumb moves the pointer, nothing scrolls */
    restart(&r, 0);
    f[0].x_um = 60000; f[0].y_um = 76000; f[1].x_um = 62000; f[1].y_um = 40000;
    land(&r, 1, f);
    for (i = 0; i < 20; i++) feed(&r, 8, 0, 1, f);
    land(&r, 2, f);
    {
        struct finger g[2];
        unsigned k;
        g[0] = f[0]; g[1] = f[1];
        for (k = 1; k <= 25; k++) {                     /* 20 mm at 100 mm/s, thumb still */
            g[1].x_um = f[1].x_um + (int32_t)(20000 * k / 25);
            feed(&r, 8, 0, 2, g);
        }
    }
    CHECK(r.sum_dx == GOLD_THUMB_DX && r.sum_wheel == 0 && (state->events & M1TP_MT_EV_THUMB));
    lift(&r);
    total += r.reports;

    /* 9. A palm-sized touch moves nothing. */
    restart(&r, 0);
    f[0].x_um = 20000; f[0].y_um = 30000; f[0].major = 850;
    land(&r, 1, f); travel(&r, 0, 1, f, 30000, 0, 100, 0); lift(&r);
    f[0].major = 0;
    CHECK(r.reports == 0 && (state->events & M1TP_MT_EV_PALM));
    /* 10. A hovering finger (below the touch size) moves nothing. */
    f[0].major = 60;
    land(&r, 1, f); travel(&r, 0, 1, f, 30000, 0, 100, 0); lift(&r);
    f[0].major = 0;
    CHECK(r.reports == 0);
    /* 11. Disable-while-typing: a touch that lands within 200 ms of a key is
     * ignored until typing has stopped. */
    m1tp_mt_key(state, r.now_us, 1, 0, 0);
    f[0].x_um = 20000; f[0].y_um = 30000;
    feed(&r, 40, 0, 1, f);
    travel(&r, 0, 1, f, 8000, 0, 100, 0);               /* 80 ms: still typing */
    CHECK(r.reports == 0);
    travel(&r, 0, 1, f, 30000, 0, 100, 0);              /* released 200 ms after the key */
    CHECK(r.sum_dx > 0);
    lift(&r);
    total += r.reports;
    /* 12. A click does not move the pointer until the finger has moved 1.5 mm. */
    restart(&r, 0);
    f[0].x_um = 40000; f[0].y_um = 30000;
    land(&r, 1, f);
    feed(&r, 8, 1, 1, f);
    f[0].x_um += 1000; feed(&r, 8, 1, 1, f);
    CHECK(r.left == 1 && r.sum_dx == 0);
    travel(&r, 1, 1, f, 10000, 0, 50, 0);               /* a drag */
    CHECK(r.sum_dx > 0 && r.buttons == 1);
    feed(&r, 8, 0, 1, f); lift(&r);
    CHECK(r.buttons == 0);
    total += r.reports;
    /* 13. A contact that jumps 25 mm between two reports is not motion. */
    restart(&r, 0);
    f[0].x_um = 40000; f[0].y_um = 30000;
    land(&r, 1, f);
    f[0].x_um += 25000; feed(&r, 8, 0, 1, f);
    CHECK(r.reports == 0);
    lift(&r);

    if (checks) *checks = number;
    if (reports) *reports = total;
    if (measured) *measured = m;
    return 0;
}
