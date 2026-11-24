/* Synthetic protocol state, timing and send-ownership tests. */
#include "control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static unsigned checks, groups;
static void init(struct rtc_context *c) {
    memset(c, 0, sizeof(*c)); CHECK(rtc_init(c, 1, 1000000) == RTC_OK);
}
static struct rtc_packet peek(struct rtc_context *c) {
    struct rtc_packet p; CHECK(rtc_tx_peek(c, 2, &p) == RTC_OK); return p;
}
static void accept(struct rtc_context *c) {
    struct rtc_packet p = peek(c); CHECK(rtc_tx_commit(c, 2, p.ticket, RTC_ACCEPTED) == RTC_OK);
}
static void hello(struct rtc_context *c) {
    init(c); accept(c); CHECK(rtc_receive(c, 2, 0, UINT64_C(0x00100000000c000b)) == RTC_OK);
    accept(c);
}
static void discovered(struct rtc_context *c) {
    hello(c); CHECK(rtc_receive(c, 2, 0, UINT64_C(0x0088000100000003)) == RTC_OK); accept(c);
}
static void failed(struct rtc_context *c, enum rtc_code why) {
    struct rtc_result r; CHECK(rtc_inspect(c, &r) == RTC_OK);
    CHECK(r.phase == RTC_FAILED && r.error == why);
    CHECK(rtc_poll(c, 3) == why);
    CHECK(rtc_receive(c, 3, 0, UINT64_C(0x00100000000c000b)) == why);
    struct rtc_result out; memset(&out, 0x5a, sizeof(out));
    struct rtc_result saved = out;
    CHECK(rtc_finish(c, 3, &out) == why); CHECK(!memcmp(&out, &saved, sizeof(out)));
    CHECK(rtc_init(c, 1, 1000) == RTC_STATE);
}
static void test_delivery(void) {
    ++groups; struct rtc_context c; init(&c);
    struct rtc_packet p = peek(&c), again;
    CHECK(p.endpoint == 0 && p.word == UINT64_C(0x0060000000000220) && p.ticket.sequence == 1);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_WOULD_BLOCK) == RTC_OK);
    CHECK(c.result.phase == RTC_WAKE_QUEUED && !c.result.accepted_tx);
    CHECK(rtc_tx_peek(&c, 2, &again) == RTC_OK);
    CHECK(again.word == p.word && again.endpoint == p.endpoint && again.ticket.sequence == p.ticket.sequence);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_OK);
    CHECK(c.result.phase == RTC_WAIT_HELLO && c.result.accepted_tx == 1);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x00100000000c000b)) == RTC_OK);
    p = peek(&c); CHECK(p.word == UINT64_C(0x00200000000c000c) && p.ticket.sequence == 2);
    CHECK(!c.result.negotiated_version);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_OK);
    CHECK(c.result.negotiated_version == 12);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x008000000000011f)) == RTC_OK);
    p = peek(&c); CHECK(p.word == UINT64_C(0x0080000000000001));
    CHECK(!c.result.endpoint_map[0] && !c.result.required_system_endpoints);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_OK);
    CHECK(c.result.endpoint_map[0] == 0x11f && c.result.required_system_endpoints == 0x11e);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000003)) == RTC_OK);
    p = peek(&c); CHECK(p.word == UINT64_C(0x0088000100000000));
    CHECK(c.result.phase == RTC_MAP_ACK_QUEUED && !c.result.endpoint_map[1]);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_OK);
    struct rtc_result out; CHECK(rtc_finish(&c, 2, &out) == RTC_OK);
    CHECK(out.phase == RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY && out.endpoint_map[1] == 3);
    CHECK(out.accepted_tx == 4 && out.received == 3 && !out.buffer.outstanding);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000003)) == RTC_STATE); failed(&c, RTC_STATE);
}
static void test_tokens(void) {
    ++groups; struct rtc_context c, other;
    init(&c); CHECK(rtc_tx_commit(&c, 2, (struct rtc_ticket){&c, 1}, RTC_ACCEPTED) == RTC_STATE);
    failed(&c, RTC_STATE);
    init(&c); init(&other); struct rtc_packet p = peek(&c), foreign = peek(&other);
    CHECK(p.ticket.sequence == foreign.ticket.sequence);
    CHECK(rtc_tx_commit(&c, 2, foreign.ticket, RTC_ACCEPTED) == RTC_TOKEN); failed(&c, RTC_TOKEN);
    init(&c); p = peek(&c); ++p.ticket.sequence;
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_TOKEN); failed(&c, RTC_TOKEN);
    init(&c); p = peek(&c); CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_OK);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_STATE);
    init(&c); p = peek(&c); accept(&c);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x00100000000c000b)) == RTC_OK); (void)peek(&c);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_ACCEPTED) == RTC_TOKEN);
    for (unsigned i = 0; i < 2; ++i) {
        init(&c); p = peek(&c);
        enum rtc_code expected = i ? RTC_SEND_UNCERTAIN : RTC_SEND_FAILED;
        CHECK(rtc_tx_commit(&c, 2, p.ticket, i ? RTC_DELIVERY_UNCERTAIN : RTC_DELIVERY_FAILED) == expected);
        CHECK(!c.result.accepted_tx); failed(&c, expected);
    }
    init(&c); p = peek(&c);
    CHECK(rtc_tx_commit(&c, 2, p.ticket, (enum rtc_delivery)99) == RTC_ARGUMENT);
    init(&c); struct rtc_context copy = c; CHECK(rtc_poll(&copy, 2) == RTC_OWNER);
    for (unsigned i = 0; i < 2; ++i) {
        init(&c); accept(&c); c.next_sequence = i ? UINT64_MAX : 0;
        CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x00100000000c000b)) == RTC_TOKEN);
        failed(&c, RTC_TOKEN);
    }
}
static void test_timing(void) {
    ++groups; struct rtc_context c;
    memset(&c, 0, sizeof(c)); CHECK(rtc_init(&c, 0, 5) == RTC_ARGUMENT);
    CHECK(rtc_init(&c, 1, 0) == RTC_ARGUMENT);
    CHECK(rtc_init(&c, 1, RTC_MAX_TIMEOUT_NS + 1) == RTC_ARGUMENT);
    CHECK(rtc_init(&c, UINT64_MAX, 1) == RTC_ARGUMENT);
    CHECK(rtc_init(&c, UINT64_MAX - 10, 10) == RTC_OK);
    CHECK(rtc_poll(&c, UINT64_MAX - 1) == RTC_OK);
    CHECK(rtc_poll(&c, UINT64_MAX) == RTC_TIMEOUT); failed(&c, RTC_TIMEOUT);
    init(&c); CHECK(rtc_poll(&c, 2) == RTC_OK); CHECK(rtc_poll(&c, 1) == RTC_CLOCK); failed(&c, RTC_CLOCK);
    init(&c); CHECK(rtc_poll(&c, 0) == RTC_CLOCK);
    init(&c); CHECK(rtc_poll(&c, 1000001) == RTC_TIMEOUT);
    init(&c); CHECK(rtc_poll(&c, 1000000) == RTC_OK);
    CHECK(c.deadline_ns == 1000001); /* Poll never refreshes deadline. */
    CHECK(rtc_poll(&c, 1000001) == RTC_TIMEOUT);
    for (unsigned mode = 0; mode < 3; ++mode) {
        init(&c); struct rtc_packet p = peek(&c);
        while (c.result.steps < RTC_STEP_LIMIT) {
            if (mode == 0) CHECK(rtc_poll(&c, 2) == RTC_OK);
            if (mode == 1) CHECK(rtc_tx_peek(&c, 2, &p) == RTC_OK);
            if (mode == 2) CHECK(rtc_tx_commit(&c, 2, p.ticket, RTC_WOULD_BLOCK) == RTC_OK);
        }
        CHECK(rtc_poll(&c, 2) == RTC_BUDGET); failed(&c, RTC_BUDGET);
    }
    discovered(&c); struct rtc_result out;
    CHECK(rtc_finish(&c, 1000001, &out) == RTC_TIMEOUT);
}
static void test_protocol(void) {
    ++groups; struct rtc_context c;
    for (unsigned lo = 0; lo <= 13; ++lo) for (unsigned hi = 0; hi <= 13; ++hi) {
        init(&c); accept(&c);
        uint64_t word = UINT64_C(0x0010000000000000) | ((uint64_t)hi << 16) | lo;
        enum rtc_code r = rtc_receive(&c, 2, 0, word);
        if (lo <= hi && lo <= 12 && hi >= 11) {
            CHECK(r == RTC_OK); struct rtc_packet p = peek(&c);
            unsigned want = hi < 12 ? hi : 12;
            CHECK(p.word == (UINT64_C(0x0020000000000000) | ((uint64_t)want << 16) | want));
        } else CHECK(r == RTC_VERSION);
    }
    init(&c); accept(&c);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x00100000ffff000b)) == RTC_OK); accept(&c);
    CHECK(c.result.negotiated_version == 12);
    init(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x00100000000c000b)) == RTC_STATE);
    init(&c); accept(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000003)) == RTC_MESSAGE);
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x00100000000c000b)) == RTC_MESSAGE);
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0070000000000020)) == RTC_MESSAGE);
    hello(&c); CHECK(rtc_receive(&c, 2, 256, 0) == RTC_MESSAGE);
    hello(&c); CHECK(rtc_receive(&c, 2, 32, 0) == RTC_MESSAGE);
    hello(&c); CHECK(rtc_receive(&c, 2, 3, 0) == RTC_UNSUPPORTED_SYSTEM_MESSAGE);
    for (unsigned bit = 0; bit < 64; ++bit) {
        uint64_t mask = UINT64_C(1) << bit;
        if (bit >= 32 && !(bit >= 52 && bit < 60)) {
            init(&c); accept(&c);
            CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x00100000000c000b) | mask) == RTC_MESSAGE);
        }
        if (bit >= 35 && bit != 51 && !(bit >= 52 && bit < 60)) {
            hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000003) | mask) == RTC_MESSAGE);
        }
    }
}
static void test_maps(void) {
    ++groups; struct rtc_context c; hello(&c);
    for (unsigned base = 0; base < 8; ++base) {
        uint32_t bits = base == 0 ? 0x11f : base == 1 ? 3 : UINT32_MAX;
        uint64_t word = UINT64_C(0x0080000000000000) | ((uint64_t)base << 32) | bits;
        if (base == 7) word |= UINT64_C(1) << 51;
        CHECK(rtc_receive(&c, 2, 0, word) == RTC_OK);
        CHECK(c.result.endpoint_map[base] == 0); accept(&c);
        CHECK(c.result.endpoint_map[base] == bits);
    }
    struct rtc_result out; CHECK(rtc_finish(&c, 2, &out) == RTC_OK);
    CHECK(out.endpoint_map[7] == UINT32_MAX && c.seen_bases == 255);
    for (unsigned base = 0; base < 8; ++base) {
        hello(&c); uint64_t word = UINT64_C(0x0080000000000000) | ((uint64_t)base << 32);
        CHECK(rtc_receive(&c, 2, 0, word) == RTC_OK); accept(&c);
        CHECK(rtc_receive(&c, 2, 0, word) == RTC_DUPLICATE_MAP); failed(&c, RTC_DUPLICATE_MAP);
    }
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0080000100000003)) == RTC_OK); accept(&c);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000007)) == RTC_DUPLICATE_MAP);
    for (unsigned missing = 0; missing < 3; ++missing) {
        hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000000) | missing) == RTC_MISSING_GPU_ENDPOINTS);
    }
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000000000001)) == RTC_MISSING_GPU_ENDPOINTS);
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0080000000000020)) == RTC_UNSUPPORTED_ENDPOINT);
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000003)) == RTC_OK);
    CHECK(rtc_finish(&c, 2, &out) == RTC_STATE); /* Queued final ACK is insufficient. */
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0080000000000001)) == RTC_OK);
    CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0088000100000003)) == RTC_STATE);
    hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x0080000100000003)) == RTC_OK); accept(&c);
    CHECK(rtc_finish(&c, 2, &out) == RTC_STATE); /* No LAST. */
}
static void test_buffers(void) {
    ++groups; struct rtc_context c;
    const unsigned eps[] = {1, 2, 4};
    for (unsigned k = 0; k < 3; ++k) for (unsigned addr = 0; addr < 2; ++addr) {
        hello(&c); CHECK(rtc_receive(&c, 2, 0, UINT64_C(0x008000000000011f)) == RTC_OK); accept(&c);
        uint64_t raw = UINT64_C(0x0010200000000000) | (addr ? UINT64_C(0x1234000) : 0);
        CHECK(rtc_receive(&c, 2, eps[k], raw) == RTC_BLOCKED_SYSTEM_BUFFER);
        CHECK(c.result.buffer.raw_word == raw && c.result.buffer.requested_bytes == 8192);
        CHECK(c.result.buffer.address_field == (addr ? UINT64_C(0x1234000) : 0));
        CHECK(c.result.buffer.common_layout && c.result.buffer.outstanding && c.result.buffer.advertised);
        failed(&c, RTC_BLOCKED_SYSTEM_BUFFER);
        CHECK(c.result.buffer.raw_word == raw);
    }
    init(&c); CHECK(rtc_receive(&c, 2, 1, UINT64_C(0x0010100000000000)) == RTC_BLOCKED_SYSTEM_BUFFER);
    CHECK(!c.result.buffer.advertised && !c.result.accepted_tx && c.result.phase == RTC_FAILED);
    for (unsigned bit = 42; bit < 44; ++bit) {
        init(&c); uint64_t raw = UINT64_C(0x0010100000000000) | (UINT64_C(1) << bit);
        CHECK(rtc_receive(&c, 2, 2, raw) == RTC_AMBIGUOUS_BUFFER);
        CHECK(c.result.buffer.raw_word == raw && !c.result.buffer.common_layout && c.result.buffer.outstanding);
    }
    init(&c); CHECK(rtc_receive(&c, 2, 8, UINT64_C(0x0102000000000000)) == RTC_AMBIGUOUS_BUFFER);
    CHECK(c.result.buffer.outstanding && !c.result.buffer.common_layout);
    init(&c); CHECK(rtc_receive(&c, 2, 4, UINT64_C(0x0010000000000000)) == RTC_MESSAGE);
    CHECK(c.result.buffer.outstanding);
    discovered(&c); CHECK(rtc_receive(&c, 2, 1, UINT64_C(0x0010100000000000)) == RTC_BLOCKED_SYSTEM_BUFFER);
    failed(&c, RTC_BLOCKED_SYSTEM_BUFFER);
}
int main(void) {
    test_delivery(); test_tokens(); test_timing(); test_protocol(); test_maps(); test_buffers();
    printf("PASS %u groups %u checks\n", groups, checks); return 0;
}
