/* Original portable validators for frozen example-http-v1. No I/O/allocation. */
#ifndef DL_EGRESS_VALIDATE_H
#define DL_EGRESS_VALIDATE_H
#include <stddef.h>
#include <stdint.h>

#define EG_DNS_MAX 512u
#define EG_DNS_QUERY_BYTES 29u
#define EG_HTTP_MAX 65536u
#define EG_HTTP_HEADER_END_MAX 8192u
#define EG_HTTP_REQUEST_BYTES 186u

enum eg_status {
    EG_OK = 0, EG_MORE, EG_ARGUMENT, EG_DNS_SIZE, EG_DNS_QUERY,
    EG_DNS_SOURCE, EG_DNS_CORRELATION, EG_DNS_NAME, EG_DNS_RECORD,
    EG_DNS_CHAIN, EG_DNS_ADDRESS, EG_DNS_TTL, EG_HTTP_LIMIT,
    EG_HTTP_STATUS, EG_HTTP_HEADER, EG_HTTP_ENCODING, EG_HTTP_FRAMING,
    EG_HTTP_TRUNCATED, EG_HTTP_CONTENT
};
struct eg_dns_answer {
    uint8_t ipv4[4][4]; /* original answer order, selected address is ipv4[0] */
    uint32_t lease_seconds; /* min(chain/A TTLs,60), never zero on success */
    unsigned count;
};
enum eg_http_framing { EG_HTTP_NONE=0, EG_HTTP_LENGTH, EG_HTTP_CHUNKED, EG_HTTP_EOF };
struct eg_http_facts {
    size_t header_bytes, body_bytes;
    enum eg_http_framing framing;
    unsigned framing_complete, eof_observed, example_content;
};

/* All spans/output objects must be valid and disjoint. Integers in API are
 * host order; IPv4 addresses are four wire-order bytes, never host integers.
 * Result structs are zeroed before validation, including errors/MORE. */
enum eg_status eg_dns_query(uint16_t id, uint8_t *out, size_t cap, size_t *written);
/* Validates exact canonical outbound query + reply ID/question and actual
 * recvfrom source10.0.2.3:53. Caller still owns UDP local-port correlation,
 * timeout, retries, duplicate policy, lease clock and socket lifecycle. */
enum eg_status eg_dns_reply(const uint8_t *query, size_t query_bytes,
    const uint8_t *reply, size_t reply_bytes, const uint8_t source_ipv4[4],
    uint16_t source_port, struct eg_dns_answer *out);
int eg_ipv4_global(const uint8_t ipv4[4]);
enum eg_status eg_http_request(const char *nonce, size_t nonce_bytes,
    uint8_t *out, size_t cap, size_t *written);
/* Validate one accumulated ordered TCP response span, not packet sequence.
 * EOF is explicit0/1 from recv==0, not a timeout/EAGAIN. EG_MORE never means
 * success, even with framing_complete=1. EG_OK requires EOF and complete valid
 * framing/content; error terminates the caller's attempt. No sticky state,
 * allocation, clocks, hidden input consumption or stream resynchronization. */
enum eg_status eg_http_response(const uint8_t *wire, size_t bytes, int eof,
                               struct eg_http_facts *out);
const char *eg_status_name(enum eg_status status);
#endif
