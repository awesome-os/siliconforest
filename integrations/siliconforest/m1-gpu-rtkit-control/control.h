/* Original source; license selection pending. No hardware transport. */
#ifndef RTC_CONTROL_H
#define RTC_CONTROL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RTC_STEP_LIMIT 128u
#define RTC_MAX_TIMEOUT_NS UINT64_C(5000000000)
enum rtc_code {
    RTC_OK, RTC_ARGUMENT, RTC_OWNER, RTC_CLOCK, RTC_TIMEOUT, RTC_BUDGET,
    RTC_STATE, RTC_TOKEN, RTC_SEND_FAILED, RTC_SEND_UNCERTAIN,
    RTC_MESSAGE, RTC_VERSION, RTC_DUPLICATE_MAP, RTC_UNSUPPORTED_ENDPOINT,
    RTC_MISSING_GPU_ENDPOINTS, RTC_BLOCKED_SYSTEM_BUFFER,
    RTC_AMBIGUOUS_BUFFER, RTC_UNSUPPORTED_SYSTEM_MESSAGE
};
enum rtc_phase {
    RTC_UNINITIALIZED, RTC_WAKE_QUEUED, RTC_WAIT_HELLO, RTC_HELLO_ACK_QUEUED,
    RTC_WAIT_MAP, RTC_MAP_ACK_QUEUED, RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY,
    RTC_FAILED
};
enum rtc_delivery { RTC_ACCEPTED, RTC_WOULD_BLOCK, RTC_DELIVERY_FAILED,
                    RTC_DELIVERY_UNCERTAIN };
struct rtc_context;
struct rtc_ticket { const struct rtc_context *owner; uint64_t sequence; };
struct rtc_packet { struct rtc_ticket ticket; uint64_t word; uint32_t endpoint; };
struct rtc_buffer_evidence {
    uint64_t raw_word, observed_ns, requested_bytes, address_field;
    uint32_t endpoint;
    unsigned outstanding, common_layout, advertised;
};
struct rtc_result {
    enum rtc_phase phase;
    enum rtc_code error;
    unsigned steps, accepted_tx, received, negotiated_version;
    uint32_t endpoint_map[8];
    uint32_t required_system_endpoints;
    struct rtc_buffer_evidence buffer;
};
/* Completely zeroed, stable-address storage. Fields are implementation state.
 * Caller serializes every call; no callbacks/concurrent mutation. Never copy,
 * reset or reinitialize, or recycle while a ticket remains accessible.
 * All output objects must be valid storage disjoint from this context. */
struct rtc_context {
    const struct rtc_context *self;
    struct rtc_result result;
    struct rtc_packet pending;
    uint64_t last_ns, deadline_ns, next_sequence;
    uint32_t staged_bitmap;
    unsigned seen_bases, staged_base, staged_last, candidate_version, offered;
};
/* Time is caller-supplied monotonic nanoseconds, nonzero and nondecreasing.
 * Every timed call (including peek, WOULD_BLOCK, poll and finish) consumes one
 * of128 steps and checks the same fixed deadline. Equality with deadline fails.
 * init counts as step1. Host/future transport authority is outside this API. */
enum rtc_code rtc_init(struct rtc_context *, uint64_t now_ns, uint64_t timeout_ns);
enum rtc_code rtc_tx_peek(struct rtc_context *, uint64_t now_ns, struct rtc_packet *);
enum rtc_code rtc_tx_commit(struct rtc_context *, uint64_t now_ns,
                           struct rtc_ticket, enum rtc_delivery);
/* endpoint is already normalized by the transport, not raw mailbox metadata.
 * A system-buffer request is preserved and blocked even if a TX is pending;
 * it never advances protocol state or causes a reply/address adoption. */
enum rtc_code rtc_receive(struct rtc_context *, uint64_t now_ns,
                         uint32_t endpoint, uint64_t word);
enum rtc_code rtc_poll(struct rtc_context *, uint64_t now_ns);
/* Writes result only on complete protocol-only discovery. Failure unchanged. */
enum rtc_code rtc_finish(struct rtc_context *, uint64_t now_ns, struct rtc_result *);
/* Untimed diagnostic snapshot, never a success/ownership/clock validation. */
enum rtc_code rtc_inspect(const struct rtc_context *, struct rtc_result *);
const char *rtc_code_name(enum rtc_code);
const char *rtc_phase_name(enum rtc_phase);
#ifdef __cplusplus
}
#endif
#endif
