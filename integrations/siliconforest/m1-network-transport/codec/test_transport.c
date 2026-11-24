#define _POSIX_C_SOURCE 200809L
#include "transport.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const uint8_t nonce[24] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23
};
static unsigned tests;
struct sink { unsigned frames, packets; size_t bytes; uint8_t last[DLNET_MTU]; int refuse; };
static int receive(void *ctx, uint8_t type, uint64_t seq, const uint8_t *p, size_t n) {
    struct sink *s = ctx;
    assert(seq > 0);
    ++s->frames;
    if (type == DLNET_IPV4) {
        ++s->packets; s->bytes += n;
        assert(n <= sizeof s->last); memcpy(s->last, p, n);
    } else assert(!n);
    return s->refuse;
}
static void put16(uint8_t *p, unsigned n) { p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n; }
static void repair_ip(uint8_t *p) {
    unsigned sum = 0;
    size_t i, ihl = (size_t)(p[0] & 15) * 4;
    p[10] = p[11] = 0;
    for (i = 0; i < ihl; i += 2) sum += ((unsigned)p[i] << 8) | p[i+1];
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    put16(p + 10, (~sum) & 65535);
}
static void packet(uint8_t *p, size_t n, unsigned ihl) {
    size_t i;
    assert(n >= ihl && n <= DLNET_MTU && ihl >= 20 && ihl <= 60 && !(ihl % 4));
    memset(p, 0, n); p[0] = (uint8_t)(0x40 | (ihl / 4));
    put16(p + 2, (unsigned)n); p[8] = 64; p[9] = 253;
    p[12] = 10; p[13] = 253; p[15] = 2;
    p[16] = 10; p[17] = 253; p[19] = 1;
    for (i = ihl; i < n; ++i) p[i] = (uint8_t)(i * 29);
    repair_ip(p);
}
static void repair_frame(uint8_t *p, size_t n) {
    uint32_t crc;
    memset(p + 48, 0, 4); crc = dln_crc32c(p, n);
    p[48] = (uint8_t)(crc >> 24); p[49] = (uint8_t)(crc >> 16);
    p[50] = (uint8_t)(crc >> 8); p[51] = (uint8_t)crc;
}
static size_t encode(struct dln_tx *tx, unsigned type, const uint8_t *p, size_t n, uint8_t *out) {
    size_t written = 999;
    assert(dln_encode(tx, (uint8_t)type, p, n, out, DLNET_FRAME_BYTES, &written) == DLN_OK);
    assert(written == DLNET_HEADER_BYTES + n); return written;
}
static void init(struct dln_tx *tx, struct dln_rx *rx, struct sink *sink) {
    memset(sink, 0, sizeof *sink);
    assert(dln_tx_init(tx, nonce, DLNET_GUEST_TO_HOST) == DLN_OK);
    assert(dln_rx_init(rx, nonce, DLNET_GUEST_TO_HOST) == DLN_OK);
}
static void opened(struct dln_tx *tx, struct dln_rx *rx, struct sink *s) {
    uint8_t frame[DLNET_FRAME_BYTES]; size_t n;
    init(tx, rx, s); n = encode(tx, DLNET_OPEN, NULL, 0, frame);
    assert(dln_feed(rx, frame, n, receive, s) == DLN_OK);
}
static void basics(void) {
    uint8_t p[DLNET_MTU], parsed[24], unchanged[24];
    struct dln_tx tx; struct dln_rx rx; struct sink s;
    assert(dln_crc32c((const uint8_t *)"123456789", 9) == UINT32_C(0xe3069283));
    assert(dln_crc32c(NULL, 0) == 0);
    memset(parsed, 42, sizeof parsed); memcpy(unchanged, parsed, sizeof parsed);
    assert(!dln_nonce_hex("wrong", 5, parsed)); assert(!memcmp(parsed, unchanged, 24));
    assert(!dln_nonce_hex("000102030405060708090a0b0c0d0e0f101112131415161A", 48, parsed));
    assert(!memcmp(parsed, unchanged, 24));
    assert(dln_nonce_hex("000102030405060708090a0b0c0d0e0f1011121314151617", 48, parsed));
    assert(!memcmp(parsed, nonce, 24));
    packet(p, 20, 20); assert(dln_ipv4_valid(p, 20));
    packet(p, 1500, 60); assert(dln_ipv4_valid(p, 1500));
    p[0] = 0x65; assert(!dln_ipv4_valid(p, 1500));
    packet(p, 20, 20); p[0] = 0x44; assert(!dln_ipv4_valid(p, 20));
    packet(p, 20, 20); p[0] = 0x4f; assert(!dln_ipv4_valid(p, 20));
    packet(p, 20, 20); p[3] = 21; assert(!dln_ipv4_valid(p, 20));
    packet(p, 20, 20); p[8] ^= 1; assert(!dln_ipv4_valid(p, 20));
    assert(!dln_ipv4_valid(NULL, 0));
    assert(dln_tx_init(NULL, nonce, 1) == DLN_ARGUMENT);
    assert(dln_rx_init(&rx, nonce, 3) == DLN_ARGUMENT);
    init(&tx, &rx, &s); assert(dln_end(&rx) == DLN_TRUNCATED);
    assert(dln_feed(&rx, NULL, 0, receive, &s) == DLN_TRUNCATED);
    ++tests;
}
static void boundaries(void) {
    struct dln_tx tx, saved; struct dln_rx rx; struct sink s;
    uint8_t out[DLNET_FRAME_BYTES], p[DLNET_MTU]; size_t n;
    init(&tx, &rx, &s); saved = tx; packet(p, 20, 20);
    assert(dln_encode(&tx, DLNET_IPV4, p, 20, out, sizeof out, &n) == DLN_STATE);
    assert(n == 0 && !memcmp(&tx, &saved, sizeof tx));
    assert(dln_encode(&tx, DLNET_OPEN, NULL, 0, out, 55, &n) == DLN_LENGTH);
    assert(n == 0 && !memcmp(&tx, &saved, sizeof tx));
    n = encode(&tx, DLNET_OPEN, NULL, 0, out);
    assert(!memcmp(out, "DLNET001", 8));
    assert(out[8] == 1 && out[9] == 1 && out[14] == 0 && out[15] == 56);
    assert(out[23] == 1 && !memcmp(out + 24, nonce, 24));
    assert(dln_feed(&rx, out, n, receive, &s) == DLN_OK);
    saved = tx;
    assert(dln_encode(&tx, DLNET_OPEN, NULL, 0, out, sizeof out, &n) == DLN_STATE);
    assert(dln_encode(&tx, DLNET_IPV4, p, 19, out, sizeof out, &n) == DLN_LENGTH);
    assert(dln_encode(&tx, DLNET_IPV4, p, 1501, out, sizeof out, &n) == DLN_LENGTH);
    assert(dln_encode(&tx, DLNET_CLOSE, p, 1, out, sizeof out, &n) == DLN_LENGTH);
    p[0] = 0; assert(dln_encode(&tx, DLNET_IPV4, p, 20, out, sizeof out, &n) == DLN_IPV4);
    assert(!memcmp(&tx, &saved, sizeof tx));
    tx.next = UINT64_MAX; rx.next = UINT64_MAX; packet(p, 20, 20);
    assert(dln_encode(&tx, DLNET_IPV4, p, 20, out, sizeof out, &n) == DLN_SEQUENCE);
    n = encode(&tx, DLNET_CLOSE, NULL, 0, out);
    assert(dln_feed(&rx, out, n, receive, &s) == DLN_OK);
    assert(dln_end(&rx) == DLN_OK && dln_end(&rx) == DLN_OK);
    assert(dln_encode(&tx, DLNET_CLOSE, NULL, 0, out, sizeof out, &n) == DLN_STATE);
    assert(dln_feed(&rx, out, 1, receive, &s) == DLN_STATE);
    ++tests;
}
static void splits(void) {
    struct dln_tx tx; struct dln_rx rx; struct sink s;
    uint8_t stream[3 * DLNET_FRAME_BYTES], p[DLNET_MTU];
    size_t n = 0, off, split, len;
    init(&tx, &rx, &s); packet(p, sizeof p, 60);
    n += encode(&tx, DLNET_OPEN, NULL, 0, stream + n);
    n += encode(&tx, DLNET_IPV4, p, sizeof p, stream + n);
    n += encode(&tx, DLNET_CLOSE, NULL, 0, stream + n);
    for (split = 1; split <= n; ++split) {
        assert(dln_rx_init(&rx, nonce, 1) == DLN_OK); memset(&s, 0, sizeof s);
        assert(dln_feed(&rx, stream, split, receive, &s) == DLN_OK);
        assert(dln_feed(&rx, stream + split, n - split, receive, &s) == DLN_OK);
        assert(dln_end(&rx) == DLN_OK && s.frames == 3 && s.packets == 1);
        assert(s.bytes == sizeof p && !memcmp(s.last, p, sizeof p));
    }
    for (len = 1; len <= 67; ++len) {
        assert(dln_rx_init(&rx, nonce, 1) == DLN_OK); memset(&s, 0, sizeof s);
        for (off = 0; off < n; off += split) {
            split = n - off; if (split > len) split = len;
            assert(dln_feed(&rx, stream + off, split, receive, &s) == DLN_OK);
        }
        assert(dln_end(&rx) == DLN_OK && s.frames == 3);
    }
    /* Every truncated prefix, including a complete DATA without CLOSE, fails. */
    for (off = 0; off < n; ++off) {
        assert(dln_rx_init(&rx, nonce, 1) == DLN_OK); memset(&s, 0, sizeof s);
        assert(dln_feed(&rx, stream, off, receive, &s) == DLN_OK);
        assert(dln_end(&rx) == DLN_TRUNCATED);
    }
    ++tests;
}
static void bad_frames(void) {
    struct dln_tx tx; struct dln_rx rx; struct sink s;
    uint8_t frame[DLNET_FRAME_BYTES], bad[DLNET_FRAME_BYTES], p[64];
    size_t n, i; unsigned bit;
    opened(&tx, &rx, &s); packet(p, sizeof p, 20);
    n = encode(&tx, DLNET_IPV4, p, sizeof p, frame);
    /* Every single bit in every byte: no corrupted packet reaches callback. */
    for (i = 0; i < n; ++i) for (bit = 0; bit < 8; ++bit) {
        struct dln_rx copy = rx; struct sink check = s; enum dln_error error;
        memcpy(bad, frame, n); bad[i] ^= (uint8_t)(1u << bit);
        error = dln_feed(&copy, bad, n, receive, &check);
        /* A corrupt larger (but capped) length may need more bytes; EOF must
         * then fail without delivering a packet. Codec itself owns no clock. */
        if (error == DLN_OK) error = dln_end(&copy);
        assert(error != DLN_OK && check.frames == 1 && check.packets == 0);
        assert(dln_feed(&copy, frame, n, receive, &check) == error);
    }
    /* Correct CRC does not bypass session, direction, sequence, length or IP. */
    { const size_t offsets[] = {0, 9, 10, 14, 23, 24, 52};
      const enum dln_error errors[] = {DLN_HEADER, DLN_DIRECTION, DLN_HEADER,
          DLN_HEADER, DLN_SEQUENCE, DLN_SESSION, DLN_HEADER};
      for (i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
          struct dln_rx copy = rx; struct sink check = s;
          memcpy(bad, frame, n); bad[offsets[i]] ^= 1; repair_frame(bad, n);
          assert(dln_feed(&copy, bad, n, receive, &check) == errors[i]);
          assert(check.frames == 1);
      }
    }
    memcpy(bad, frame, n); put16(bad + 12, 1501);
    assert(dln_feed(&rx, bad, 56, receive, &s) == DLN_LENGTH);
    assert(rx.used == 56 && rx.accepted == 1);
    opened(&tx, &rx, &s); memcpy(bad, frame, n); bad[56] = 0x65; repair_frame(bad, n);
    assert(dln_feed(&rx, bad, n, receive, &s) == DLN_IPV4 && s.packets == 0);
    opened(&tx, &rx, &s); s.refuse = 1;
    assert(dln_feed(&rx, frame, n, receive, &s) == DLN_CALLBACK);
    assert(dln_feed(&rx, frame, n, receive, &s) == DLN_CALLBACK && s.packets == 1);
    /* Replay, skipped sequence, junk prefix and post-CLOSE bytes fail closed. */
    opened(&tx, &rx, &s); assert(dln_feed(&rx, frame, n, receive, &s) == DLN_OK);
    assert(dln_feed(&rx, frame, n, receive, &s) == DLN_SEQUENCE);
    init(&tx, &rx, &s); memset(bad, 'x', 56);
    assert(dln_feed(&rx, bad, 56, receive, &s) == DLN_HEADER);
    opened(&tx, &rx, &s); n = encode(&tx, DLNET_CLOSE, NULL, 0, frame);
    frame[n] = 0;
    assert(dln_feed(&rx, frame, n + 1, receive, &s) == DLN_STATE && s.frames == 2);
    ++tests;
}
static void wait_fd(int fd, short events) {
    struct pollfd p = {fd, events, 0}; int rc;
    do { rc = poll(&p, 1, 1000); } while (rc < 0 && errno == EINTR);
    assert(rc == 1 && (p.revents & events));
}
static void socket_stream(void) {
    int fds[2], flags; unsigned direction;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa); sa.sa_handler = SIG_IGN;
    assert(sigemptyset(&sa.sa_mask) == 0 && sigaction(SIGPIPE, &sa, NULL) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    for (direction = 0; direction < 2; ++direction) {
        flags = fcntl(fds[direction], F_GETFL); assert(flags >= 0);
        assert(fcntl(fds[direction], F_SETFL, flags | O_NONBLOCK) == 0);
        assert(fcntl(fds[direction], F_GETFL) & O_NONBLOCK);
    }
    for (direction = 0; direction < 2; ++direction) {
        struct dln_tx tx; struct dln_rx rx; struct sink s;
        uint8_t frame[DLNET_FRAME_BYTES], ip[DLNET_MTU], readback[31];
        unsigned k;
        memset(&s, 0, sizeof s);
        assert(dln_tx_init(&tx, nonce, (uint8_t)(direction + 1)) == DLN_OK);
        assert(dln_rx_init(&rx, nonce, (uint8_t)(direction + 1)) == DLN_OK);
        for (k = 0; k < 130; ++k) {
            size_t n, sent = 0, got = 0, len = 20 + (k * 83u) % 1481u;
            unsigned type = k == 0 ? DLNET_OPEN : k == 129 ? DLNET_CLOSE : DLNET_IPV4;
            packet(ip, len, 20);
            n = encode(&tx, type, type == DLNET_IPV4 ? ip : NULL,
                       type == DLNET_IPV4 ? len : 0, frame);
            while (sent < n) {
                size_t part = n - sent; ssize_t w, rd;
                if (part > 17) part = 17;
                wait_fd(fds[direction], POLLOUT);
                w = write(fds[direction], frame + sent, part); assert(w > 0);
                sent += (size_t)w;
                while (got < sent) {
                    wait_fd(fds[1-direction], POLLIN);
                    rd = read(fds[1-direction], readback, sizeof readback); assert(rd > 0);
                    got += (size_t)rd;
                    assert(dln_feed(&rx, readback, (size_t)rd, receive, &s) == DLN_OK);
                }
            }
        }
        assert(s.frames == 130 && s.packets == 128 && dln_end(&rx) == DLN_OK);
    }
    assert(shutdown(fds[0], SHUT_WR) == 0);
    wait_fd(fds[1], POLLIN);
    { uint8_t b; assert(read(fds[1], &b, 1) == 0); }
    assert(close(fds[0]) == 0 && close(fds[1]) == 0);
    ++tests;
}
int main(void) {
    basics(); boundaries(); splits(); bad_frames(); socket_stream();
    printf("PASS %u groups: original codec, corruption/EOF, real host socket streams only\n", tests);
    return 0;
}
