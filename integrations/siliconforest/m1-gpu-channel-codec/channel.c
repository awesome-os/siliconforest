/* Original portable byte codec; no transport, allocation or publication. */
#include "channel.h"
#define PAGE UINT64_C(0x4000)
static int profile_ok(enum agxc_profile p) { return p == AGXC_V13_5_G13_SOURCE_ONLY; }
static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    if (an - 1 > UINTPTR_MAX - x || bn - 1 > UINTPTR_MAX - y) return 1;
    return x <= y + bn - 1 && y <= x + an - 1;
}
static uint64_t get(const uint8_t *p, unsigned n) {
    uint64_t v = 0; for (unsigned i = 0; i < n; ++i) v |= (uint64_t)p[i] << (8 * i); return v;
}
static void put(uint8_t *p, unsigned n, uint64_t v) {
    for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static int canonical(uint64_t a) {
    const uint64_t mask = (UINT64_C(1) << 40) - 1;
    uint64_t low = a & mask;
    return a == (low & (UINT64_C(1) << 39) ? low | ~mask : low);
}
static int span(uint64_t a, uint64_t n) {
    return a && n && !(a % PAGE) && !(n % PAGE) && n - 1 <= UINT64_MAX - a &&
        canonical(a) && canonical(a + n - 1) && ((a ^ (a + n - 1)) >> 39) == 0;
}
static enum agxc_result facts_valid(struct agxc_range_facts f) {
    if (f.count > AGXC_RANGE_MAX || (f.count && !f.ranges)) return AGXC_RANGE;
    for (size_t i = 0; i < f.count; ++i) {
        uint64_t a = f.ranges[i].address, n = f.ranges[i].bytes;
        if (!span(a, n)) return AGXC_RANGE;
        for (size_t j = 0; j < i; ++j) {
            uint64_t b = f.ranges[j].address, m = f.ranges[j].bytes;
            if (a <= b + m - 1 && b <= a + n - 1) return AGXC_RANGE;
        }
    }
    return AGXC_OK;
}
static enum agxc_result address_valid(uint64_t a, struct agxc_range_facts f) {
    enum agxc_result r = facts_valid(f); if (r) return r;
    if (!a) return AGXC_OK; /* Source default; no invented completion meaning. */
    if (!span(a, PAGE)) return AGXC_RANGE;
    for (size_t i = 0; i < f.count; ++i)
        if (a >= f.ranges[i].address && a - f.ranges[i].address <= f.ranges[i].bytes - PAGE) return AGXC_OK;
    return AGXC_RANGE;
}
enum agxc_result agxc_select(enum agxc_firmware v, enum agxc_gpu g,
    enum agxc_evidence evidence, enum agxc_profile *out) {
    if (!out) return AGXC_ARGUMENT;
    if (evidence != AGXC_PINNED_SOURCE_TEST) return AGXC_ABI_UNKNOWN;
    if (v != AGXC_V13_5 || g != AGXC_G13) return AGXC_ABI_UNKNOWN;
    *out = AGXC_V13_5_G13_SOURCE_ONLY; return AGXC_OK;
}
enum agxc_result agxc_dc_encode(enum agxc_profile p, enum agxc_dc_pattern kind, uint8_t *out, size_t n) {
    if (!profile_ok(p)) return AGXC_PROFILE;
    if (!out) return AGXC_ARGUMENT;
    if (n != 0x30) return AGXC_SIZE;
    if (kind != AGXC_DC_INIT && kind != AGXC_DC_UPDATE_IDLE_TS) return AGXC_OPCODE;
    put(out, 4, (uint32_t)kind);
    for (size_t i = 4; i < n; ++i) out[i] = 0;
    return AGXC_OK;
}
enum agxc_result agxc_dc_decode(enum agxc_profile p, const uint8_t *in, size_t n, enum agxc_dc_pattern *out) {
    if (!profile_ok(p)) return AGXC_PROFILE;
    if (!in || !out || overlap(in, n, out, sizeof(*out))) return AGXC_ARGUMENT;
    if (n != 0x30) return AGXC_SIZE;
    uint32_t kind = (uint32_t)get(in, 4);
    if (kind != AGXC_DC_INIT && kind != AGXC_DC_UPDATE_IDLE_TS) return AGXC_OPCODE;
    for (size_t i = 4; i < n; ++i) if (in[i]) return AGXC_PATTERN;
    *out = (enum agxc_dc_pattern)kind; return AGXC_OK;
}
enum agxc_result agxc_fwctl_encode(enum agxc_profile p, const struct agxc_fwctl_pattern *in,
    struct agxc_range_facts f, uint8_t *out, size_t n) {
    if (!profile_ok(p)) return AGXC_PROFILE;
    if (!in || !out) return AGXC_ARGUMENT;
    if (n != 0x14) return AGXC_SIZE;
    enum agxc_result r = address_valid(in->address, f); if (r) return r;
    if (overlap(out, n, in, sizeof(*in)) || overlap(out, n, f.ranges, f.count * sizeof(*f.ranges))) return AGXC_ARGUMENT;
    put(out, 8, in->address); put(out + 8, 4, 0); put(out + 12, 4, in->context_id);
    put(out + 16, 2, 1); put(out + 18, 2, 2); return AGXC_OK;
}
enum agxc_result agxc_fwctl_decode(enum agxc_profile p, const uint8_t *in, size_t n,
    struct agxc_range_facts f, struct agxc_fwctl_pattern *out) {
    if (!profile_ok(p)) return AGXC_PROFILE;
    if (!in || !out) return AGXC_ARGUMENT;
    if (n != 0x14) return AGXC_SIZE;
    enum agxc_result r = facts_valid(f); if (r) return r;
    if (overlap(out, sizeof(*out), in, n) || overlap(out, sizeof(*out), f.ranges, f.count * sizeof(*f.ranges))) return AGXC_ARGUMENT;
    if (get(in + 8, 4) || get(in + 16, 2) != 1 || get(in + 18, 2) != 2) return AGXC_PATTERN;
    struct agxc_fwctl_pattern v = {get(in, 8), (uint32_t)get(in + 12, 4)};
    r = address_valid(v.address, f); if (r) return r;
    *out = v; return AGXC_OK;
}
static enum agxc_result sizes(enum agxc_channel ch, size_t *state, size_t *write, size_t *stride) {
    if (ch == AGXC_DEVCTRL) { *state = 0x30; *write = 0x20; *stride = 0x30; return AGXC_OK; }
    if (ch == AGXC_FWCTL) { *state = 0x20; *write = 0x10; *stride = 0x14; return AGXC_OK; }
    return AGXC_ARGUMENT;
}
enum agxc_result agxc_state_decode(enum agxc_profile p, enum agxc_channel ch,
    const uint8_t *in, size_t n, struct agxc_ring_snapshot *out) {
    if (!profile_ok(p)) return AGXC_PROFILE;
    if (!in || !out || overlap(in, n, out, sizeof(*out))) return AGXC_ARGUMENT;
    size_t state, write, stride; enum agxc_result r = sizes(ch, &state, &write, &stride); if (r) return r;
    if (n != state) return AGXC_SIZE;
    struct agxc_ring_snapshot v = {(uint32_t)get(in, 4), (uint32_t)get(in + write, 4)};
    if (v.read_index >= AGXC_SLOTS || v.write_index >= AGXC_SLOTS) return AGXC_INDEX;
    *out = v; return AGXC_OK;
}
enum agxc_result agxc_ring_candidate(enum agxc_profile p, enum agxc_channel ch,
    struct agxc_ring_snapshot s, int tx, struct agxc_ring_candidate *out) {
    if (!profile_ok(p)) return AGXC_PROFILE;
    if (!out || (tx != 0 && tx != 1)) return AGXC_ARGUMENT;
    size_t state, write, stride; enum agxc_result r = sizes(ch, &state, &write, &stride); if (r) return r;
    if (s.read_index >= AGXC_SLOTS || s.write_index >= AGXC_SLOTS) return AGXC_INDEX;
    uint32_t occupied = (s.write_index + AGXC_SLOTS - s.read_index) % AGXC_SLOTS;
    if (tx && occupied == AGXC_SLOTS - 1) return AGXC_FULL;
    if (!tx && !occupied) return AGXC_EMPTY;
    uint32_t index = tx ? s.write_index : s.read_index;
    *out = (struct agxc_ring_candidate){index, (index + 1) % AGXC_SLOTS,
        occupied, AGXC_SLOTS - 1 - occupied, index * stride}; return AGXC_OK;
}
