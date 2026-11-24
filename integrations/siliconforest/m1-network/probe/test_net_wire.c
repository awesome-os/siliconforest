/* Host-only tests for net_wire.c. Build: cc -std=c11 -Wall -Wextra -Werror
 * -fsanitize=address,undefined net_wire.c test_net_wire.c -o t && ./t */
#include "net_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t MA[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0a};
static const uint8_t MB[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0b};
static const uint8_t BC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const char TAG[DLN_TAG_BYTES + 1] = "0123456789abcdef01234567";
#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static void probe_frames(void)
{
    uint8_t f[DLN_ETH_MIN_FRAME];
    assert(dln_probe_frame(f, BC, MA, 1, 7, TAG) == 60);
    assert(memcmp(f, BC, 6) == 0 && memcmp(f + 6, MA, 6) == 0);
    assert(f[12] == 0x88 && f[13] == 0xb5);
    assert(dln_probe_frame_match(f, 60, MA, 1, 7, TAG));
    assert(!dln_probe_frame_match(f, 60, MB, 1, 7, TAG));      /* wrong source */
    assert(!dln_probe_frame_match(f, 60, MA, 2, 7, TAG));      /* wrong dir */
    assert(!dln_probe_frame_match(f, 60, MA, 1, 8, TAG));      /* wrong seq */
    assert(!dln_probe_frame_match(f, 43, MA, 1, 7, TAG));      /* truncated */
    f[43] ^= 1;
    assert(!dln_probe_frame_match(f, 60, MA, 1, 7, TAG));      /* tag changed */
    assert(!dln_probe_frame_match(NULL, 60, MA, 1, 7, TAG));
}

static void arp(void)
{
    uint8_t f[DLN_ETH_MIN_FRAME];
    /* A request is the reply layout with op 1 and broadcast destination. */
    dln_arp_reply(f, MA, IP(10, 250, 0, 1), BC, IP(10, 250, 0, 2));
    assert(f[12] == 0x08 && f[13] == 0x06 && f[21] == 2);
    assert(!dln_arp_request_match(f, 60, MA, IP(10, 250, 0, 1), IP(10, 250, 0, 2)));
    f[21] = 1;
    assert(dln_arp_request_match(f, 60, MA, IP(10, 250, 0, 1), IP(10, 250, 0, 2)));
    assert(!dln_arp_request_match(f, 60, MB, IP(10, 250, 0, 1), IP(10, 250, 0, 2)));
    assert(!dln_arp_request_match(f, 60, MA, IP(10, 250, 0, 1), IP(10, 250, 0, 3)));
    assert(!dln_arp_request_match(f, 41, MA, IP(10, 250, 0, 1), IP(10, 250, 0, 2)));
    dln_arp_reply(f, MB, IP(10, 250, 0, 2), MA, IP(10, 250, 0, 1));
    assert(memcmp(f, MA, 6) == 0 && memcmp(f + 6, MB, 6) == 0 && f[21] == 2);
    assert(memcmp(f + 22, MB, 6) == 0 && memcmp(f + 32, MA, 6) == 0);
    assert(f[28] == 10 && f[29] == 250 && f[30] == 0 && f[31] == 2);
}

static void checksum_and_udp(void)
{
    /* RFC 1071 example words 0001 f203 f4f5 f6f7 sum to ddf2; checksum 220d. */
    static const uint8_t w[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    uint8_t pkt[256], eth[300];
    uint32_t s, d;
    uint16_t sp, dp;
    const uint8_t *pl;
    size_t pn, n;
    assert(dln_ip_checksum(w, sizeof w) == 0x220d);
    n = dln_ipv4_udp(pkt, sizeof pkt, IP(10, 251, 0, 2), IP(10, 251, 0, 1), 9, 40000,
                     (const uint8_t *)"hello", 5, 0x1234);
    assert(n == 33 && pkt[0] == 0x45 && pkt[9] == 17);
    assert(dln_ip_checksum(pkt, 20) == 0);
    assert(dln_ipv4_udp_parse(pkt, n, &s, &d, &sp, &dp, &pl, &pn));
    assert(s == IP(10, 251, 0, 2) && d == IP(10, 251, 0, 1) && sp == 9 && dp == 40000);
    assert(pn == 5 && memcmp(pl, "hello", 5) == 0);
    pkt[12] ^= 1;
    assert(!dln_ipv4_udp_parse(pkt, n, &s, &d, &sp, &dp, &pl, &pn));   /* bad checksum */
    pkt[12] ^= 1;
    assert(!dln_ipv4_udp_parse(pkt, n - 1, &s, &d, &sp, &dp, &pl, &pn)); /* short */
    pkt[6] = 0x20;                                                        /* MF */
    assert(!dln_ipv4_udp_parse(pkt, n, &s, &d, &sp, &dp, &pl, &pn));
    pkt[6] = 0;
    assert(dln_ipv4_udp(pkt, 32, 1, 2, 3, 4, (const uint8_t *)"hello", 5, 0) == 0);
    assert(dln_ipv4_udp(pkt, sizeof pkt, 1, 2, 3, 4, NULL, 1, 0) == 0);
    memset(eth, 0, sizeof eth);
    memcpy(eth, MB, 6); memcpy(eth + 6, MA, 6); eth[12] = 0x08; eth[13] = 0x00;
    memcpy(eth + 14, pkt, n);
    assert(dln_eth_udp_match(eth, 14 + n, MA, IP(10, 251, 0, 1), 40000, (const uint8_t *)"hello", 5));
    assert(!dln_eth_udp_match(eth, 14 + n, MB, IP(10, 251, 0, 1), 40000, (const uint8_t *)"hello", 5));
    assert(!dln_eth_udp_match(eth, 14 + n, MA, IP(10, 251, 0, 1), 9, (const uint8_t *)"hello", 5));
    assert(!dln_eth_udp_match(eth, 14 + n, MA, IP(10, 251, 0, 1), 40000, (const uint8_t *)"hellp", 5));
}

struct seen { int count; size_t last; int stop_at; };
static int visit(const uint8_t *p, size_t caplen, size_t datalen, void *ctx)
{
    struct seen *s = ctx;
    (void)p; (void)datalen;
    s->count++;
    s->last = caplen;
    return s->count == s->stop_at ? 42 : 0;
}
static size_t rec(uint8_t *b, uint32_t caplen, uint16_t hdrlen)
{
    uint32_t datalen = caplen;
    /* Header bytes only: some cases deliberately claim more than the buffer. */
    memset(b, 0xee, DLN_BPF_MIN_HDR);
    memcpy(b + DLN_BPF_CAPLEN_OFF, &caplen, 4);
    memcpy(b + DLN_BPF_DATALEN_OFF, &datalen, 4);
    memcpy(b + DLN_BPF_HDRLEN_OFF, &hdrlen, 2);
    return ((size_t)hdrlen + caplen + 3u) & ~(size_t)3u;
}
static void bpf_walk(void)
{
    uint8_t b[512];
    struct seen s = {0, 0, 0};
    size_t n = rec(b, 60, 18);
    n += rec(b + n, 42, 18);
    assert(n == 80 + 60);
    assert(dln_bpf_walk(b, n, visit, &s) == 0 && s.count == 2 && s.last == 42);
    s.count = 0; s.stop_at = 1;
    assert(dln_bpf_walk(b, n, visit, &s) == 42 && s.count == 1);
    s.count = 0; s.stop_at = 0;
    assert(dln_bpf_walk(b, n - 1, visit, &s) == -1);          /* truncated last record */
    rec(b, 1000, 18);
    assert(dln_bpf_walk(b, 100, visit, &s) == -1);            /* caplen beyond buffer */
    rec(b, 10, 4);
    assert(dln_bpf_walk(b, 100, visit, &s) == -1);            /* hdrlen too small */
    assert(dln_bpf_walk(b, 0, visit, &s) == 0);
    assert(dln_bpf_walk(b, 5, visit, &s) == -1);
}

static void text(void)
{
    char out[8];
    dln_sanitize(out, sizeof out, "ab c\x01\xff=zzzz");
    assert(strcmp(out, "ab_c__=") == 0);
    dln_sanitize(out, 1, "abc");
    assert(out[0] == '\0');
    dln_sanitize(out, sizeof out, NULL);
    assert(out[0] == '\0');
    assert(dln_nonce_ok("0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(!dln_nonce_ok("0123456789abcdef0123456789abcdef0123456789abcde"));
    assert(!dln_nonce_ok("0123456789abcdef0123456789abcdef0123456789abcdef0"));
    assert(!dln_nonce_ok("0123456789ABCDEF0123456789abcdef0123456789abcdef"));
    assert(!dln_nonce_ok(NULL));
}

int main(void)
{
    probe_frames();
    arp();
    checksum_and_udp();
    bpf_walk();
    text();
    puts("HOST ONLY PASS: probe frames, ARP, IPv4/UDP, BPF walk, sanitize/nonce");
    return 0;
}
