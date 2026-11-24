/* Original implementation; wire integers are big endian, never C structs. */
#include "transport.h"
#include <string.h>

static const uint8_t magic[8] = {'D','L','N','E','T','0','0','1'};
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t get64(const uint8_t *p) {
    return ((uint64_t)get32(p) << 32) | get32(p + 4);
}
static void put16(uint8_t *p, uint16_t x) {
    p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x;
}
static void put32(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)(x >> 24); p[1] = (uint8_t)(x >> 16);
    p[2] = (uint8_t)(x >> 8); p[3] = (uint8_t)x;
}
static void put64(uint8_t *p, uint64_t x) {
    put32(p, (uint32_t)(x >> 32)); put32(p + 4, (uint32_t)x);
}
static int direction_valid(uint8_t d) {
    return d == DLNET_GUEST_TO_HOST || d == DLNET_HOST_TO_GUEST;
}
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
int dln_nonce_hex(const char *hex, size_t n, uint8_t out[DLNET_NONCE_BYTES]) {
    uint8_t temp[DLNET_NONCE_BYTES];
    size_t i;
    if (!hex || !out || n != 2 * DLNET_NONCE_BYTES) return 0;
    for (i = 0; i < DLNET_NONCE_BYTES; ++i) {
        int a = hex_digit(hex[2*i]), b = hex_digit(hex[2*i+1]);
        if (a < 0 || b < 0) return 0;
        temp[i] = (uint8_t)((a << 4) | b);
    }
    memcpy(out, temp, sizeof temp);
    return 1;
}
static uint32_t crc_byte(uint32_t crc, uint8_t b) {
    unsigned i;
    crc ^= b;
    for (i = 0; i < 8; ++i)
        crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0x82f63b78) : 0);
    return crc;
}
uint32_t dln_crc32c(const uint8_t *p, size_t n) {
    uint32_t crc = UINT32_MAX;
    size_t i;
    for (i = 0; i < n; ++i) crc = crc_byte(crc, p[i]);
    return ~crc;
}
static uint32_t frame_crc(const uint8_t *p, size_t n) {
    uint32_t crc = UINT32_MAX;
    size_t i;
    for (i = 0; i < n; ++i)
        crc = crc_byte(crc, (i >= 48 && i < 52) ? 0 : p[i]);
    return ~crc;
}
int dln_ipv4_valid(const uint8_t *p, size_t n) {
    size_t ihl, i;
    uint32_t sum = 0;
    if (!p || n < 20 || n > DLNET_MTU || (p[0] >> 4) != 4) return 0;
    ihl = (size_t)(p[0] & 15) * 4;
    if (ihl < 20 || ihl > n || get16(p + 2) != n) return 0;
    for (i = 0; i < ihl; i += 2) sum += get16(p + i);
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return sum == 65535;
}
static enum dln_error shape(uint8_t type, size_t n, uint8_t opened) {
    if (!opened) return type == DLNET_OPEN && !n ? DLN_OK : DLN_STATE;
    if (type == DLNET_IPV4) return n >= 20 && n <= DLNET_MTU ? DLN_OK : DLN_LENGTH;
    if (type == DLNET_CLOSE) return n == 0 ? DLN_OK : DLN_LENGTH;
    return DLN_STATE;
}
enum dln_error dln_tx_init(struct dln_tx *s, const uint8_t *nonce, uint8_t d) {
    if (!s || !nonce || !direction_valid(d)) return DLN_ARGUMENT;
    memset(s, 0, sizeof *s); memcpy(s->nonce, nonce, DLNET_NONCE_BYTES);
    s->next = 1; s->direction = d;
    return DLN_OK;
}
enum dln_error dln_encode(struct dln_tx *s, uint8_t type, const uint8_t *p,
                         size_t n, uint8_t *out, size_t cap, size_t *written) {
    enum dln_error e;
    if (written) *written = 0;
    if (!s || !out || !written || (n && !p) || !direction_valid(s->direction))
        return DLN_ARGUMENT;
    if (s->closed || !s->next) return DLN_STATE;
    e = shape(type, n, s->opened);
    if (e) return e;
    if (s->next == UINT64_MAX && type != DLNET_CLOSE) return DLN_SEQUENCE;
    if (type == DLNET_IPV4 && !dln_ipv4_valid(p, n)) return DLN_IPV4;
    if (cap < DLNET_HEADER_BYTES + n) return DLN_LENGTH;
    /* Input/output buffers must be disjoint. Caller owns exact I/O of frame. */
    memset(out, 0, DLNET_HEADER_BYTES); memcpy(out, magic, sizeof magic);
    out[8] = type; out[9] = s->direction;
    put16(out + 12, (uint16_t)n); put16(out + 14, DLNET_HEADER_BYTES);
    put64(out + 16, s->next); memcpy(out + 24, s->nonce, DLNET_NONCE_BYTES);
    if (n) memcpy(out + DLNET_HEADER_BYTES, p, n);
    put32(out + 48, frame_crc(out, DLNET_HEADER_BYTES + n));
    *written = DLNET_HEADER_BYTES + n;
    s->opened = 1;
    if (type == DLNET_CLOSE) s->closed = 1;
    else ++s->next;
    return DLN_OK;
}
enum dln_error dln_rx_init(struct dln_rx *s, const uint8_t *nonce, uint8_t d) {
    if (!s || !nonce || !direction_valid(d)) return DLN_ARGUMENT;
    memset(s, 0, sizeof *s); memcpy(s->nonce, nonce, DLNET_NONCE_BYTES);
    s->next = 1; s->direction = d; s->need = DLNET_HEADER_BYTES;
    return DLN_OK;
}
static enum dln_error header(struct dln_rx *s) {
    const uint8_t *p = s->frame;
    size_t n = get16(p + 12);
    enum dln_error e;
    if (memcmp(p, magic, sizeof magic) || get16(p + 10) ||
        get16(p + 14) != DLNET_HEADER_BYTES || get32(p + 52)) return DLN_HEADER;
    if (memcmp(p + 24, s->nonce, DLNET_NONCE_BYTES)) return DLN_SESSION;
    if (p[9] != s->direction) return DLN_DIRECTION;
    if (get64(p + 16) != s->next || !s->next ||
        (s->next == UINT64_MAX && p[8] != DLNET_CLOSE)) return DLN_SEQUENCE;
    /* Reject an oversized payload before waiting for or copying any of it. */
    if (n > DLNET_MTU) return DLN_LENGTH;
    e = shape(p[8], n, s->opened);
    if (e) return e;
    s->need = DLNET_HEADER_BYTES + n;
    return DLN_OK;
}
enum dln_error dln_feed(struct dln_rx *s, const uint8_t *p, size_t n,
                       dln_receive receive, void *context) {
    if (!s) return DLN_ARGUMENT;
    if (s->error) return s->error;
    if ((!p && n) || !receive) return s->error = DLN_ARGUMENT;
    if (s->ended) return s->error = DLN_STATE;
    while (n) {
        size_t take;
        if (s->closed) return s->error = DLN_STATE;
        take = s->need - s->used;
        if (take > n) take = n;
        memcpy(s->frame + s->used, p, take);
        s->used += take; p += take; n -= take;
        if (s->used == DLNET_HEADER_BYTES && s->need == DLNET_HEADER_BYTES) {
            s->error = header(s);
            if (s->error) return s->error;
        }
        if (s->used == s->need) {
            size_t bytes = s->need - DLNET_HEADER_BYTES;
            uint8_t type = s->frame[8];
            if (get32(s->frame + 48) != frame_crc(s->frame, s->need))
                return s->error = DLN_INTEGRITY;
            if (type == DLNET_IPV4 && !dln_ipv4_valid(s->frame + DLNET_HEADER_BYTES, bytes))
                return s->error = DLN_IPV4;
            s->opened = 1;
            if (type == DLNET_CLOSE) s->closed = 1;
            ++s->accepted;
            if (receive(context, type, s->next, s->frame + DLNET_HEADER_BYTES, bytes))
                return s->error = DLN_CALLBACK;
            if (!s->closed) ++s->next;
            s->used = 0; s->need = DLNET_HEADER_BYTES;
        }
    }
    return DLN_OK;
}
enum dln_error dln_end(struct dln_rx *s) {
    if (!s) return DLN_ARGUMENT;
    if (s->error) return s->error;
    if (!s->closed || s->used) return s->error = DLN_TRUNCATED;
    s->ended = 1;
    return DLN_OK;
}
const char *dln_error_name(enum dln_error e) {
    static const char *const names[] = {"ok","argument","state","header",
        "session","direction","sequence","length","integrity","ipv4",
        "callback","truncated"};
    if ((unsigned)e >= sizeof names / sizeof names[0]) return "unknown";
    return names[e];
}
