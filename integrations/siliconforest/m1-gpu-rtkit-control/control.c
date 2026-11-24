/* Original bounded RTKit control transaction. MIT references are documented in
 * PROVENANCE.md; no hardware access, addresses imported, or upstream code copied. */
#include "control.h"
#include <string.h>
#define TYPE(n) ((uint64_t)(n) << 52)
#define HELLO_MASK (TYPE(255) | UINT64_C(0xffffffff))
#define MAP_MASK (TYPE(255) | (UINT64_C(1) << 51) | (UINT64_C(7) << 32) | UINT64_C(0xffffffff))
#define COMMON_BUFFER_MASK (TYPE(255) | (UINT64_C(255) << 44) | ((UINT64_C(1) << 42) - 1))
#define KNOWN_SYSTEM UINT32_C(0x11f) /* management, crash, syslog, debug, ioreport, oslog */
static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (an - 1 > UINTPTR_MAX - x || bn - 1 > UINTPTR_MAX - y) return 1;
    return x <= y + bn - 1 && y <= x + an - 1;
}
static enum rtc_code fail(struct rtc_context *c, enum rtc_code why) {
    if (c->result.phase != RTC_FAILED) {
        c->result.phase = RTC_FAILED;
        c->result.error = why;
    }
    return c->result.error;
}
static enum rtc_code timed(struct rtc_context *c, uint64_t now) {
    if (!c || c->self != c) return RTC_OWNER;
    if (c->result.phase == RTC_FAILED) return c->result.error;
    if (!now || now < c->last_ns) return fail(c, RTC_CLOCK);
    if (now >= c->deadline_ns) return fail(c, RTC_TIMEOUT);
    if (c->result.steps >= RTC_STEP_LIMIT) return fail(c, RTC_BUDGET);
    c->last_ns = now;
    ++c->result.steps;
    return RTC_OK;
}
static int queued(enum rtc_phase p) {
    return p == RTC_WAKE_QUEUED || p == RTC_HELLO_ACK_QUEUED || p == RTC_MAP_ACK_QUEUED;
}
static enum rtc_code queue(struct rtc_context *c, enum rtc_phase phase, uint64_t word) {
    if (!c->next_sequence || c->next_sequence == UINT64_MAX) return fail(c, RTC_TOKEN);
    c->pending = (struct rtc_packet){{c, c->next_sequence++}, word, 0};
    c->offered = 0;
    c->result.phase = phase;
    return RTC_OK;
}
enum rtc_code rtc_init(struct rtc_context *c, uint64_t now, uint64_t timeout) {
    if (!c) return RTC_ARGUMENT;
    const unsigned char *b = (const unsigned char *)c;
    for (size_t i = 0; i < sizeof(*c); ++i) if (b[i]) return RTC_STATE;
    if (!now || !timeout || timeout > RTC_MAX_TIMEOUT_NS || timeout > UINT64_MAX - now)
        return RTC_ARGUMENT;
    c->self = c; c->last_ns = now; c->deadline_ns = now + timeout;
    c->result.steps = 1; c->next_sequence = 1;
    return queue(c, RTC_WAKE_QUEUED, TYPE(6) | UINT64_C(0x220));
}
enum rtc_code rtc_tx_peek(struct rtc_context *c, uint64_t now, struct rtc_packet *out) {
    enum rtc_code r = timed(c, now); if (r) return r;
    if (!out || overlaps(out, sizeof(*out), c, sizeof(*c))) return fail(c, RTC_ARGUMENT);
    if (!queued(c->result.phase)) return fail(c, RTC_STATE);
    *out = c->pending; c->offered = 1;
    return RTC_OK;
}
enum rtc_code rtc_tx_commit(struct rtc_context *c, uint64_t now,
                           struct rtc_ticket t, enum rtc_delivery delivery) {
    enum rtc_code r = timed(c, now); if (r) return r;
    if (!queued(c->result.phase) || !c->offered) return fail(c, RTC_STATE);
    if (t.owner != c || !t.sequence || t.sequence != c->pending.ticket.sequence)
        return fail(c, RTC_TOKEN);
    if (delivery == RTC_WOULD_BLOCK) return RTC_OK;
    if (delivery == RTC_DELIVERY_FAILED) return fail(c, RTC_SEND_FAILED);
    if (delivery == RTC_DELIVERY_UNCERTAIN) return fail(c, RTC_SEND_UNCERTAIN);
    if (delivery != RTC_ACCEPTED) return fail(c, RTC_ARGUMENT);
    ++c->result.accepted_tx;
    switch (c->result.phase) {
    case RTC_WAKE_QUEUED: c->result.phase = RTC_WAIT_HELLO; break;
    case RTC_HELLO_ACK_QUEUED:
        c->result.negotiated_version = c->candidate_version;
        c->result.phase = RTC_WAIT_MAP; break;
    case RTC_MAP_ACK_QUEUED:
        c->result.endpoint_map[c->staged_base] = c->staged_bitmap;
        c->seen_bases |= 1u << c->staged_base;
        c->result.required_system_endpoints = c->result.endpoint_map[0] & ~UINT32_C(1);
        c->result.phase = c->staged_last ? RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY : RTC_WAIT_MAP;
        break;
    default: return fail(c, RTC_STATE);
    }
    c->offered = 0;
    return RTC_OK;
}
static enum rtc_code buffer_request(struct rtc_context *c, uint64_t now,
                                    uint32_t endpoint, uint64_t word) {
    struct rtc_buffer_evidence *b = &c->result.buffer;
    b->raw_word = word; b->observed_ns = now; b->endpoint = endpoint; b->outstanding = 1;
    b->advertised = !!(c->result.endpoint_map[endpoint / 32] & (UINT32_C(1) << (endpoint % 32)));
    if (endpoint == 8 || (word & ~COMMON_BUFFER_MASK)) return fail(c, RTC_AMBIGUOUS_BUFFER);
    b->requested_bytes = ((word >> 44) & 255) * UINT64_C(4096);
    b->address_field = word & ((UINT64_C(1) << 42) - 1);
    b->common_layout = 1;
    /* Parsed fields are diagnostic only; no reply, allocation or translation. */
    return fail(c, b->requested_bytes ? RTC_BLOCKED_SYSTEM_BUFFER : RTC_MESSAGE);
}
enum rtc_code rtc_receive(struct rtc_context *c, uint64_t now, uint32_t endpoint, uint64_t word) {
    enum rtc_code r = timed(c, now); if (r) return r;
    ++c->result.received;
    if (endpoint > 255) return fail(c, RTC_MESSAGE);
    unsigned type = (unsigned)((word >> 52) & 255);
    if (((endpoint == 1 || endpoint == 2 || endpoint == 4) && type == 1) ||
        (endpoint == 8 && (word >> 56) == 1))
        return buffer_request(c, now, endpoint, word);
    if (queued(c->result.phase)) return fail(c, RTC_STATE);
    if (endpoint) return fail(c, endpoint < 32 ? RTC_UNSUPPORTED_SYSTEM_MESSAGE : RTC_MESSAGE);
    if (c->result.phase == RTC_WAIT_HELLO) {
        if (type != 1 || (word & ~HELLO_MASK)) return fail(c, RTC_MESSAGE);
        unsigned lo = (unsigned)(word & 65535), hi = (unsigned)((word >> 16) & 65535);
        if (lo > hi || lo > 12 || hi < 11) return fail(c, RTC_VERSION);
        c->candidate_version = hi < 12 ? hi : 12;
        return queue(c, RTC_HELLO_ACK_QUEUED,
                     TYPE(2) | ((uint64_t)c->candidate_version << 16) | c->candidate_version);
    }
    if (c->result.phase == RTC_WAIT_MAP) {
        if (type != 8 || (word & ~MAP_MASK)) return fail(c, RTC_MESSAGE);
        unsigned base = (unsigned)((word >> 32) & 7), last = (unsigned)((word >> 51) & 1);
        uint32_t bitmap = (uint32_t)word;
        if (c->seen_bases & (1u << base)) return fail(c, RTC_DUPLICATE_MAP);
        if (!base && (bitmap & ~KNOWN_SYSTEM)) return fail(c, RTC_UNSUPPORTED_ENDPOINT);
        uint32_t app = base == 1 ? bitmap : c->result.endpoint_map[1];
        if (last && (app & 3) != 3) return fail(c, RTC_MISSING_GPU_ENDPOINTS);
        c->staged_base = base; c->staged_bitmap = bitmap; c->staged_last = last;
        return queue(c, RTC_MAP_ACK_QUEUED, TYPE(8) | ((uint64_t)base << 32) |
                     (last ? UINT64_C(1) << 51 : UINT64_C(1)));
    }
    return fail(c, RTC_STATE);
}
enum rtc_code rtc_poll(struct rtc_context *c, uint64_t now) { return timed(c, now); }
enum rtc_code rtc_finish(struct rtc_context *c, uint64_t now, struct rtc_result *out) {
    enum rtc_code r = timed(c, now); if (r) return r;
    if (!out || overlaps(out, sizeof(*out), c, sizeof(*c))) return fail(c, RTC_ARGUMENT);
    if (c->result.phase != RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY ||
        c->result.buffer.outstanding || (c->result.endpoint_map[1] & 3) != 3)
        return fail(c, RTC_STATE);
    *out = c->result; return RTC_OK;
}
enum rtc_code rtc_inspect(const struct rtc_context *c, struct rtc_result *out) {
    if (!c || c->self != c) return RTC_OWNER;
    if (!out || overlaps(out, sizeof(*out), c, sizeof(*c))) return RTC_ARGUMENT;
    *out = c->result; return RTC_OK;
}
const char *rtc_code_name(enum rtc_code c) {
    static const char *const names[] = {"OK", "ARGUMENT", "OWNER", "CLOCK", "TIMEOUT", "BUDGET",
        "STATE", "TOKEN", "SEND_FAILED", "SEND_UNCERTAIN", "MESSAGE", "VERSION", "DUPLICATE_MAP",
        "UNSUPPORTED_ENDPOINT", "MISSING_GPU_ENDPOINTS", "BLOCKED_SYSTEM_BUFFER",
        "AMBIGUOUS_BUFFER", "UNSUPPORTED_SYSTEM_MESSAGE"};
    return (unsigned)c < sizeof(names) / sizeof(*names) ? names[c] : "INVALID_CODE";
}
const char *rtc_phase_name(enum rtc_phase p) {
    static const char *const names[] = {"UNINITIALIZED", "WAKE_QUEUED", "WAIT_HELLO",
        "HELLO_ACK_QUEUED", "WAIT_MAP", "MAP_ACK_QUEUED", "CONTROL_DISCOVERED_PROTOCOL_ONLY", "FAILED"};
    return (unsigned)p < sizeof(names) / sizeof(*names) ? names[p] : "INVALID_PHASE";
}
