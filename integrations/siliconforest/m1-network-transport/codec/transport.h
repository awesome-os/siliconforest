/* Original Darwin-lab stream codec. No target, networking or device I/O. */
#ifndef DLNET_TRANSPORT_H
#define DLNET_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>

#define DLNET_NONCE_BYTES 24u
#define DLNET_HEADER_BYTES 56u
#define DLNET_MTU 1500u
#define DLNET_FRAME_BYTES (DLNET_HEADER_BYTES + DLNET_MTU)
#define DLNET_GUEST_TO_HOST 1u
#define DLNET_HOST_TO_GUEST 2u
#define DLNET_OPEN 1u
#define DLNET_IPV4 2u
#define DLNET_CLOSE 3u

enum dln_error {
    DLN_OK = 0, DLN_ARGUMENT, DLN_STATE, DLN_HEADER, DLN_SESSION,
    DLN_DIRECTION, DLN_SEQUENCE, DLN_LENGTH, DLN_INTEGRITY, DLN_IPV4,
    DLN_CALLBACK, DLN_TRUNCATED
};

/* Initialize before use. State is caller-owned, single-threaded, non-reentrant.
 * Never modify fields in production. No allocation or system call occurs. */
struct dln_tx {
    uint8_t nonce[DLNET_NONCE_BYTES];
    uint64_t next;
    uint8_t direction, opened, closed;
};
struct dln_rx {
    uint8_t frame[DLNET_FRAME_BYTES], nonce[DLNET_NONCE_BYTES];
    size_t used, need;
    uint64_t next, accepted;
    uint8_t direction, opened, closed, ended;
    enum dln_error error;
};

/* Called exactly once for each fully validated frame, including OPEN/CLOSE.
 * IPv4 bytes are a borrowed view valid only until this call returns. The
 * callback must not re-enter the decoder. Nonzero return is a sticky failure;
 * the frame will never be retried. No callback occurs for invalid data. */
typedef int (*dln_receive)(void *context, uint8_t type, uint64_t sequence,
                           const uint8_t *payload, size_t bytes);

int dln_nonce_hex(const char *hex48, size_t bytes, uint8_t out[DLNET_NONCE_BYTES]);
uint32_t dln_crc32c(const uint8_t *data, size_t bytes);
int dln_ipv4_valid(const uint8_t *packet, size_t bytes);
enum dln_error dln_tx_init(struct dln_tx *, const uint8_t nonce[DLNET_NONCE_BYTES],
                         uint8_t direction);
/* Failure never advances TX state, sets *written=0, never partially emits. */
enum dln_error dln_encode(struct dln_tx *, uint8_t type, const uint8_t *payload,
                         size_t bytes, uint8_t *out, size_t capacity,
                         size_t *written);
enum dln_error dln_rx_init(struct dln_rx *, const uint8_t nonce[DLNET_NONCE_BYTES],
                         uint8_t direction);
/* Arbitrary fragmentation/coalescing, fixed memory, sticky error, no resync.
 * On error callbacks for earlier complete frames remain committed. */
enum dln_error dln_feed(struct dln_rx *, const uint8_t *, size_t,
                       dln_receive, void *context);
/* EOF succeeds only after an exact CLOSE with no following/partial bytes. */
enum dln_error dln_end(struct dln_rx *);
const char *dln_error_name(enum dln_error);
#endif
