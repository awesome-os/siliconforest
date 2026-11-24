/* Portable wire helpers for darwin-net-probe. Original code; no upstream copy. */
#include "net_wire.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

size_t dln_probe_frame(uint8_t out[DLN_ETH_MIN_FRAME], const uint8_t dst[6],
                       const uint8_t src[6], uint8_t dir, uint8_t seq,
                       const char tag[DLN_TAG_BYTES])
{
    memset(out, 0, DLN_ETH_MIN_FRAME);
    memcpy(out, dst, 6);
    memcpy(out + 6, src, 6);
    put16(out + 12, DLN_ETHERTYPE_PROBE);
    memcpy(out + 14, "DL25", 4);
    out[18] = dir;
    out[19] = seq;
    memcpy(out + 20, tag, DLN_TAG_BYTES);
    return DLN_ETH_MIN_FRAME;
}

int dln_probe_frame_match(const uint8_t *f, size_t n, const uint8_t src[6],
                          uint8_t dir, uint8_t seq, const char tag[DLN_TAG_BYTES])
{
    if (!f || n < 20u + DLN_TAG_BYTES) return 0;
    if (memcmp(f + 6, src, 6) != 0 || get16(f + 12) != DLN_ETHERTYPE_PROBE) return 0;
    if (memcmp(f + 14, "DL25", 4) != 0 || f[18] != dir || f[19] != seq) return 0;
    return memcmp(f + 20, tag, DLN_TAG_BYTES) == 0;
}

int dln_arp_request_match(const uint8_t *f, size_t n, const uint8_t sender_mac[6],
                          uint32_t sender_ip, uint32_t target_ip)
{
    const uint8_t *a;
    if (!f || n < DLN_ETH_HEADER + 28u) return 0;
    if (get16(f + 12) != DLN_ETHERTYPE_ARP) return 0;
    a = f + DLN_ETH_HEADER;
    if (get16(a) != 1 || get16(a + 2) != DLN_ETHERTYPE_IPV4 || a[4] != 6 || a[5] != 4)
        return 0;
    if (get16(a + 6) != 1) return 0;                        /* request */
    if (memcmp(a + 8, sender_mac, 6) != 0) return 0;
    return get32(a + 14) == sender_ip && get32(a + 24) == target_ip;
}

size_t dln_arp_reply(uint8_t out[DLN_ETH_MIN_FRAME], const uint8_t my_mac[6],
                     uint32_t my_ip, const uint8_t to_mac[6], uint32_t to_ip)
{
    uint8_t *a = out + DLN_ETH_HEADER;
    memset(out, 0, DLN_ETH_MIN_FRAME);
    memcpy(out, to_mac, 6);
    memcpy(out + 6, my_mac, 6);
    put16(out + 12, DLN_ETHERTYPE_ARP);
    put16(a, 1);                         /* Ethernet */
    put16(a + 2, DLN_ETHERTYPE_IPV4);
    a[4] = 6; a[5] = 4;
    put16(a + 6, 2);                     /* reply */
    memcpy(a + 8, my_mac, 6);
    put32(a + 14, my_ip);
    memcpy(a + 18, to_mac, 6);
    put32(a + 24, to_ip);
    return DLN_ETH_MIN_FRAME;
}

uint16_t dln_ip_checksum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i + 1 < n; i += 2) sum += get16(p + i);
    if (n & 1u) sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

size_t dln_ipv4_udp(uint8_t *out, size_t cap, uint32_t src, uint32_t dst,
                    uint16_t sport, uint16_t dport, const uint8_t *payload,
                    size_t len, uint16_t ip_id)
{
    size_t total = DLN_IPV4_HEADER + DLN_UDP_HEADER + len;
    uint8_t *u;
    if (!out || (len && !payload) || len > 1400u || cap < total) return 0;
    memset(out, 0, DLN_IPV4_HEADER + DLN_UDP_HEADER);
    out[0] = 0x45;
    put16(out + 2, (uint16_t)total);
    put16(out + 4, ip_id);
    out[8] = 64;
    out[9] = 17;
    put32(out + 12, src);
    put32(out + 16, dst);
    put16(out + 10, dln_ip_checksum(out, DLN_IPV4_HEADER));
    u = out + DLN_IPV4_HEADER;
    put16(u, sport);
    put16(u + 2, dport);
    put16(u + 4, (uint16_t)(DLN_UDP_HEADER + len));
    put16(u + 6, 0);                     /* optional for IPv4 */
    if (len) memcpy(u + DLN_UDP_HEADER, payload, len);
    return total;
}

int dln_ipv4_udp_parse(const uint8_t *p, size_t n, uint32_t *src, uint32_t *dst,
                       uint16_t *sport, uint16_t *dport,
                       const uint8_t **payload, size_t *plen)
{
    size_t ihl, total, ulen;
    if (!p || n < DLN_IPV4_HEADER + DLN_UDP_HEADER) return 0;
    if ((p[0] >> 4) != 4) return 0;
    ihl = (size_t)(p[0] & 15u) * 4u;
    if (ihl < DLN_IPV4_HEADER || n < ihl + DLN_UDP_HEADER) return 0;
    total = get16(p + 2);
    if (total < ihl + DLN_UDP_HEADER || total > n) return 0;
    if (p[9] != 17) return 0;
    if ((get16(p + 6) & 0x3fffu) != 0) return 0;          /* no fragments */
    if (dln_ip_checksum(p, ihl) != 0) return 0;
    ulen = get16(p + ihl + 4);
    if (ulen < DLN_UDP_HEADER || ihl + ulen > total) return 0;
    if (src) *src = get32(p + 12);
    if (dst) *dst = get32(p + 16);
    if (sport) *sport = get16(p + ihl);
    if (dport) *dport = get16(p + ihl + 2);
    if (payload) *payload = p + ihl + DLN_UDP_HEADER;
    if (plen) *plen = ulen - DLN_UDP_HEADER;
    return 1;
}

int dln_eth_udp_match(const uint8_t *f, size_t n, const uint8_t src_mac[6],
                      uint32_t dst_ip, uint16_t dport, const uint8_t *want,
                      size_t want_len)
{
    uint32_t d = 0;
    uint16_t dp = 0;
    const uint8_t *pl = NULL;
    size_t pn = 0;
    if (!f || n < DLN_ETH_HEADER || memcmp(f + 6, src_mac, 6) != 0) return 0;
    if (get16(f + 12) != DLN_ETHERTYPE_IPV4) return 0;
    if (!dln_ipv4_udp_parse(f + DLN_ETH_HEADER, n - DLN_ETH_HEADER, NULL, &d, NULL,
                            &dp, &pl, &pn))
        return 0;
    return d == dst_ip && dp == dport && pn == want_len &&
           (want_len == 0 || memcmp(pl, want, want_len) == 0);
}

int dln_bpf_walk(const uint8_t *buf, size_t n, dln_bpf_visit visit, void *ctx)
{
    size_t off = 0;
    while (off < n) {
        uint32_t caplen, datalen;
        uint16_t hdrlen;
        size_t rec;
        int r;
        if (n - off < DLN_BPF_MIN_HDR) return -1;
        memcpy(&caplen, buf + off + DLN_BPF_CAPLEN_OFF, 4);
        memcpy(&datalen, buf + off + DLN_BPF_DATALEN_OFF, 4);
        memcpy(&hdrlen, buf + off + DLN_BPF_HDRLEN_OFF, 2);
        if (hdrlen < DLN_BPF_MIN_HDR || hdrlen > n - off) return -1;
        if (caplen > n - off - hdrlen || caplen > datalen) return -1;
        r = visit(buf + off + hdrlen, caplen, datalen, ctx);
        if (r) return r;
        rec = (size_t)hdrlen + caplen;
        rec = (rec + (DLN_BPF_ALIGN - 1u)) & ~(size_t)(DLN_BPF_ALIGN - 1u);
        if (rec == 0) return -1;
        off += rec;
    }
    return 0;
}

void dln_sanitize(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || !cap) return;
    if (src) {
        for (; src[i] && i + 1 < cap; i++) {
            unsigned char c = (unsigned char)src[i];
            dst[i] = (c >= 0x21 && c <= 0x7e) ? (char)c : '_';
        }
    }
    dst[i] = '\0';
}

int dln_nonce_ok(const char *s)
{
    size_t i;
    if (!s) return 0;
    for (i = 0; i < 48; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return s[48] == '\0';
}
