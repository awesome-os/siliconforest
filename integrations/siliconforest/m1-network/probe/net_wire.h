/* Portable wire helpers for darwin-net-probe. Original code; no upstream copy.
 * Host-testable: no Darwin headers, no allocation, no I/O. */
#ifndef DLN_NET_WIRE_H
#define DLN_NET_WIRE_H
#include <stddef.h>
#include <stdint.h>

#define DLN_ETHERTYPE_IPV4   0x0800u
#define DLN_ETHERTYPE_ARP    0x0806u
#define DLN_ETHERTYPE_PROBE  0x88b5u   /* IEEE 802 local experimental 1 */
#define DLN_ETH_HEADER       14u
#define DLN_ETH_MIN_FRAME    60u       /* without FCS */
#define DLN_TAG_BYTES        24u       /* nonce prefix carried in frames */
#define DLN_IPV4_HEADER      20u
#define DLN_UDP_HEADER       8u

/* Darwin LP64 user-space struct bpf_hdr (net/bpf.h, BPF_TIMEVAL=timeval32):
 * tstamp 8 bytes, caplen u32 at 8, datalen u32 at 12, hdrlen u16 at 16.
 * Records are aligned to BPF_ALIGNMENT = sizeof(int32_t). The Darwin build
 * asserts these offsets against the SDK header. */
#define DLN_BPF_CAPLEN_OFF   8u
#define DLN_BPF_DATALEN_OFF  12u
#define DLN_BPF_HDRLEN_OFF   16u
#define DLN_BPF_MIN_HDR      18u
#define DLN_BPF_ALIGN        4u

/* Probe frame: dst, src, ethertype 0x88b5, "DL25", dir, seq, 24-byte tag,
 * zero padding to 60 bytes. Returns 60. */
size_t dln_probe_frame(uint8_t out[DLN_ETH_MIN_FRAME], const uint8_t dst[6],
                       const uint8_t src[6], uint8_t dir, uint8_t seq,
                       const char tag[DLN_TAG_BYTES]);
/* 1 if the frame is a probe frame from src with this dir/seq/tag. */
int dln_probe_frame_match(const uint8_t *f, size_t n, const uint8_t src[6],
                          uint8_t dir, uint8_t seq, const char tag[DLN_TAG_BYTES]);

/* IPv4 addresses are host byte order in this API. */
/* 1 if f is an Ethernet ARP request (IPv4 over Ethernet) from sender_mac
 * asking who-has target_ip, tell sender_ip. */
int dln_arp_request_match(const uint8_t *f, size_t n, const uint8_t sender_mac[6],
                          uint32_t sender_ip, uint32_t target_ip);
/* Ethernet ARP reply from my_mac/my_ip to to_mac/to_ip, padded. Returns 60. */
size_t dln_arp_reply(uint8_t out[DLN_ETH_MIN_FRAME], const uint8_t my_mac[6],
                     uint32_t my_ip, const uint8_t to_mac[6], uint32_t to_ip);

/* RFC 1071 one's-complement checksum over n bytes. */
uint16_t dln_ip_checksum(const uint8_t *p, size_t n);
/* IPv4 + UDP packet (UDP checksum 0, IPv4 checksum set, TTL 64, DF clear).
 * Returns the packet length, or 0 if cap is too small or len too large. */
size_t dln_ipv4_udp(uint8_t *out, size_t cap, uint32_t src, uint32_t dst,
                    uint16_t sport, uint16_t dport, const uint8_t *payload,
                    size_t len, uint16_t ip_id);
/* Parses an IPv4/UDP packet with a valid header checksum. Returns 1 and the
 * fields on success; payload points into p. Returns 0 otherwise. */
int dln_ipv4_udp_parse(const uint8_t *p, size_t n, uint32_t *src, uint32_t *dst,
                       uint16_t *sport, uint16_t *dport,
                       const uint8_t **payload, size_t *plen);
/* 1 if f is an Ethernet IPv4/UDP frame from src_mac to dst_ip:dport whose
 * payload equals want/want_len. */
int dln_eth_udp_match(const uint8_t *f, size_t n, const uint8_t src_mac[6],
                      uint32_t dst_ip, uint16_t dport, const uint8_t *want,
                      size_t want_len);

/* Walks a Darwin BPF read buffer. visit returns nonzero to stop; that value
 * is returned. Returns 0 after all records, -1 for a malformed buffer. */
typedef int (*dln_bpf_visit)(const uint8_t *pkt, size_t caplen, size_t datalen,
                             void *ctx);
int dln_bpf_walk(const uint8_t *buf, size_t n, dln_bpf_visit visit, void *ctx);

/* Copies src into dst (cap >= 1), replacing bytes outside 0x21..0x7e with
 * '_' and truncating; always NUL-terminates. For values inside k=v lines. */
void dln_sanitize(char *dst, size_t cap, const char *src);
/* 1 if s is exactly 48 lowercase hex characters. */
int dln_nonce_ok(const char *s);
#endif
