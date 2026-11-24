/* darwin-net-probe: first guest networking step for the PureDarwin M1 guest.
 * Original code; no upstream copy. Root only (interface create/destroy).
 *
 * Proves the in-kernel BSD network stack and software datapaths without any
 * NIC, firmware, Wi-Fi or macOS kext:
 *   lo0 up, UDP and TCP loopback; a feth (if_fake) Ethernet pair with raw
 *   frames both ways over BPF, kernel ARP and IPv4 output across the pair;
 *   a utun interface with one packet out to user space and one injected in.
 * Optional (--skywalk): the same feth checks with net.link.fake.bsd_mode=0,
 * i.e. the pair attached as native Skywalk netif interfaces.
 *
 * Only virtual interfaces are created, and they are destroyed before exit.
 * The only existing interface changed is lo0 (127.0.0.1/8, UP), and only if
 * it is not already configured. Every wait is bounded. Output is one line
 * per step on stdout: "DL25_NET <nonce> <step> <pass|fail|skip|info> k=v...".
 * Exit 0 when no required step failed, 1 otherwise, 2 for usage. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <sys/utsname.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/bpf.h>
#include <net/if_utun.h>
#include <net/if_fake_var.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "net_wire.h"

/* Bind the portable BPF walker to the SDK's LP64 user-space layout. */
_Static_assert(offsetof(struct bpf_hdr, bh_caplen) == DLN_BPF_CAPLEN_OFF, "bpf caplen");
_Static_assert(offsetof(struct bpf_hdr, bh_datalen) == DLN_BPF_DATALEN_OFF, "bpf datalen");
_Static_assert(offsetof(struct bpf_hdr, bh_hdrlen) == DLN_BPF_HDRLEN_OFF, "bpf hdrlen");
_Static_assert(BPF_WORDALIGN(1) == DLN_BPF_ALIGN, "bpf alignment");
_Static_assert(sizeof(struct if_fake_request) == 32 + 128, "if_fake_request layout");

#define WAIT_MS 2000
#define LINE_MAX_BYTES 480
#define IPV4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define FETH_A_IP IPV4(10, 250, 0, 1)
#define FETH_B_IP IPV4(10, 250, 0, 2)
#define SKY_A_IP  IPV4(10, 252, 0, 1)
#define SKY_B_IP  IPV4(10, 252, 0, 2)
#define UTUN_LOCAL IPV4(10, 251, 0, 1)
#define UTUN_PEER  IPV4(10, 251, 0, 2)
#define DISCARD_PORT 9

static const char *g_nonce;
static char g_tag[DLN_TAG_BYTES];
static unsigned g_passed, g_failed, g_skipped;

static void emit(const char *step, const char *status, const char *fmt, ...)
{
    char body[LINE_MAX_BYTES - 120];
    va_list ap;
    body[0] = '\0';
    if (fmt) {
        va_start(ap, fmt);
        (void)vsnprintf(body, sizeof body, fmt, ap);
        va_end(ap);
    }
    printf("DL25_NET %s %s %s%s%s\n", g_nonce, step, status, body[0] ? " " : "", body);
    fflush(stdout);
}
static void result(const char *step, int ok, int required, const char *fmt, ...)
{
    char body[LINE_MAX_BYTES - 120];
    va_list ap;
    body[0] = '\0';
    if (fmt) {
        va_start(ap, fmt);
        (void)vsnprintf(body, sizeof body, fmt, ap);
        va_end(ap);
    }
    if (ok) g_passed++;
    else if (required) g_failed++;
    emit(step, ok ? "pass" : (required ? "fail" : "info"), "%s%srequired=%d",
         body, body[0] ? " " : "", required);
}
static void skip(const char *step, const char *why)
{
    g_skipped++;
    emit(step, "skip", "reason=%s", why);
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int wait_readable(int fd, int64_t deadline)
{
    struct pollfd p = {fd, POLLIN, 0};
    int64_t left = deadline - now_ms();
    int r;
    if (left <= 0) return 0;
    r = poll(&p, 1, (int)left);
    return r > 0 && (p.revents & POLLIN);
}

static struct sockaddr_in sin4(uint32_t host_ip, uint16_t port)
{
    struct sockaddr_in s;
    memset(&s, 0, sizeof s);
    s.sin_len = sizeof s;
    s.sin_family = AF_INET;
    s.sin_port = htons(port);
    s.sin_addr.s_addr = htonl(host_ip);
    return s;
}

/* ---------- interface helpers ---------- */
static int if_flags(int s, const char *name, short *flags)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strlcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
    if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) return errno;
    *flags = ifr.ifr_flags;
    return 0;
}
static int if_up(int s, const char *name)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strlcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
    if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) return errno;
    ifr.ifr_flags |= IFF_UP;
    if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) return errno;
    return 0;
}
static int if_add_inet(int s, const char *name, uint32_t addr, uint32_t peer_or_brd,
                       uint32_t mask)
{
    struct ifaliasreq ifra;
    struct sockaddr_in a = sin4(addr, 0), b = sin4(peer_or_brd, 0), m = sin4(mask, 0);
    memset(&ifra, 0, sizeof ifra);
    strlcpy(ifra.ifra_name, name, sizeof ifra.ifra_name);
    memcpy(&ifra.ifra_addr, &a, sizeof a);
    memcpy(&ifra.ifra_broadaddr, &b, sizeof b);
    memcpy(&ifra.ifra_mask, &m, sizeof m);
    if (ioctl(s, SIOCAIFADDR, &ifra) < 0) return errno;
    return 0;
}
static int if_create(int s, const char *prefix, char out[IFNAMSIZ])
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strlcpy(ifr.ifr_name, prefix, sizeof ifr.ifr_name);
    if (ioctl(s, SIOCIFCREATE, &ifr) < 0) return errno;
    strlcpy(out, ifr.ifr_name, IFNAMSIZ);
    return 0;
}
static int if_destroy(int s, const char *name)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strlcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
    if (ioctl(s, SIOCIFDESTROY, &ifr) < 0) return errno;
    return 0;
}
static int feth_peer(int s, const char *a, const char *b)
{
    struct if_fake_request iffr;
    struct ifdrv ifd;
    memset(&iffr, 0, sizeof iffr);
    strlcpy(iffr.iffr_peer_name, b, sizeof iffr.iffr_peer_name);
    memset(&ifd, 0, sizeof ifd);
    strlcpy(ifd.ifd_name, a, sizeof ifd.ifd_name);
    ifd.ifd_cmd = IF_FAKE_S_CMD_SET_PEER;
    ifd.ifd_len = sizeof iffr;
    ifd.ifd_data = &iffr;
    if (ioctl(s, SIOCSDRVSPEC, &ifd) < 0) return errno;
    return 0;
}
/* AF_LINK lookup: MAC and packet counters. */
struct link_info { int found; uint8_t mac[6]; uint32_t ipackets, opackets; unsigned type; };
static struct link_info link_of(const char *name)
{
    struct link_info li;
    struct ifaddrs *list = NULL, *i;
    memset(&li, 0, sizeof li);
    if (getifaddrs(&list) != 0) return li;
    for (i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_LINK || strcmp(i->ifa_name, name))
            continue;
        const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)(const void *)i->ifa_addr;
        li.found = 1;
        li.type = sdl->sdl_type;
        if (sdl->sdl_alen == 6) memcpy(li.mac, LLADDR(sdl), 6);
        if (i->ifa_data) {
            const struct if_data *d = i->ifa_data;
            li.ipackets = d->ifi_ipackets;
            li.opackets = d->ifi_opackets;
        }
        break;
    }
    freeifaddrs(list);
    return li;
}
static int has_inet(const char *name, uint32_t addr)
{
    struct ifaddrs *list = NULL, *i;
    int found = 0;
    if (getifaddrs(&list) != 0) return 0;
    for (i = list; i; i = i->ifa_next) {
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !strcmp(i->ifa_name, name) &&
            ntohl(((const struct sockaddr_in *)(const void *)i->ifa_addr)->sin_addr.s_addr) == addr)
            found = 1;
    }
    freeifaddrs(list);
    return found;
}

/* ---------- BPF helpers ---------- */
struct bpf_dev { int fd; size_t blen; uint8_t *buf; };
static int bpf_open(const char *ifname, struct bpf_dev *b)
{
    char path[32];
    u_int one = 1, zero = 0, blen = 0;
    struct ifreq ifr;
    int i, e = ENOENT;
    b->fd = -1; b->buf = NULL; b->blen = 0;
    for (i = 0; i < 64; i++) {
        snprintf(path, sizeof path, "/dev/bpf%d", i);
        b->fd = open(path, O_RDWR);
        if (b->fd >= 0) break;
        e = errno;
        if (e != EBUSY) break;
    }
    if (b->fd < 0) return e;
    memset(&ifr, 0, sizeof ifr);
    strlcpy(ifr.ifr_name, ifname, sizeof ifr.ifr_name);
    if (ioctl(b->fd, BIOCGBLEN, &blen) < 0 || blen == 0 || blen > (1u << 20) ||
        ioctl(b->fd, BIOCSETIF, &ifr) < 0 || ioctl(b->fd, BIOCIMMEDIATE, &one) < 0 ||
        ioctl(b->fd, BIOCSHDRCMPLT, &one) < 0) {
        e = errno ? errno : EINVAL;
        close(b->fd); b->fd = -1;
        return e;
    }
    (void)ioctl(b->fd, BIOCSSEESENT, &zero);   /* receive side only, if supported */
    b->buf = malloc(blen);
    if (!b->buf) { close(b->fd); b->fd = -1; return ENOMEM; }
    b->blen = blen;
    return 0;
}
static void bpf_close(struct bpf_dev *b)
{
    if (b->fd >= 0) close(b->fd);
    free(b->buf);
    b->fd = -1; b->buf = NULL;
}
typedef int (*frame_pred)(const uint8_t *f, size_t n, void *ctx);
struct pred_ctx { frame_pred pred; void *ctx; };
static int pred_visit(const uint8_t *p, size_t caplen, size_t datalen, void *c)
{
    struct pred_ctx *pc = c;
    (void)datalen;
    return pc->pred(p, caplen, pc->ctx) ? 1 : 0;
}
/* Returns 1 when a matching frame arrives before the deadline, 0 on timeout,
 * -errno on a read error, -EBADMSG for a malformed BPF buffer. */
static int bpf_wait(struct bpf_dev *b, frame_pred pred, void *ctx, int64_t deadline)
{
    struct pred_ctx pc = {pred, ctx};
    while (wait_readable(b->fd, deadline)) {
        ssize_t n = read(b->fd, b->buf, b->blen);
        int r;
        if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; return -errno; }
        r = dln_bpf_walk(b->buf, (size_t)n, pred_visit, &pc);
        if (r == 1) return 1;
        if (r < 0) return -EBADMSG;
    }
    return 0;
}

struct probe_want { const uint8_t *src; uint8_t dir, seq; };
static int want_probe(const uint8_t *f, size_t n, void *c)
{
    struct probe_want *w = c;
    return dln_probe_frame_match(f, n, w->src, w->dir, w->seq, g_tag);
}
struct arp_want { const uint8_t *mac; uint32_t sender, target; };
static int want_arp(const uint8_t *f, size_t n, void *c)
{
    struct arp_want *w = c;
    return dln_arp_request_match(f, n, w->mac, w->sender, w->target);
}
struct udp_want { const uint8_t *mac; uint32_t dst; uint16_t port; const uint8_t *pl; size_t len; };
static int want_udp(const uint8_t *f, size_t n, void *c)
{
    struct udp_want *w = c;
    return dln_eth_udp_match(f, n, w->mac, w->dst, w->port, w->pl, w->len);
}

/* ---------- steps ---------- */
static void step_identity(void)
{
    struct utsname u;
    char sys[32], rel[32], mach[32];
    if (uname(&u) != 0) { emit("identity", "info", "uname_errno=%d", errno); return; }
    dln_sanitize(sys, sizeof sys, u.sysname);
    dln_sanitize(rel, sizeof rel, u.release);
    dln_sanitize(mach, sizeof mach, u.machine);
    emit("identity", "info", "sysname=%s release=%s machine=%s euid=%u pid=%d",
         sys, rel, mach, (unsigned)geteuid(), (int)getpid());
}
static void step_sysctls(void)
{
    static const char *names[] = {"net.link.fake.bsd_mode", "net.link.fake.nxattach",
                                  "kern.skywalk.priv_check", "net.inet.ip.forwarding"};
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        int v = 0;
        size_t len = sizeof v;
        if (sysctlbyname(names[i], &v, &len, NULL, 0) == 0)
            emit("sysctl", "info", "name=%s present=1 value=%d", names[i], v);
        else
            emit("sysctl", "info", "name=%s present=0 errno=%d", names[i], errno);
    }
}
static void step_interfaces(const char *when)
{
    struct ifaddrs *list = NULL, *i;
    unsigned n = 0;
    if (getifaddrs(&list) != 0) { emit("interfaces", "info", "when=%s errno=%d", when, errno); return; }
    for (i = list; i && n < 24; i = i->ifa_next) {
        char name[IFNAMSIZ], addr[INET_ADDRSTRLEN] = "-";
        unsigned type = 0;
        if (!i->ifa_addr) continue;
        if (i->ifa_addr->sa_family == AF_LINK)
            type = ((const struct sockaddr_dl *)(const void *)i->ifa_addr)->sdl_type;
        else if (i->ifa_addr->sa_family == AF_INET)
            inet_ntop(AF_INET, &((const struct sockaddr_in *)(const void *)i->ifa_addr)->sin_addr,
                      addr, sizeof addr);
        else continue;
        dln_sanitize(name, sizeof name, i->ifa_name);
        emit("interfaces", "info", "when=%s index=%u name=%s family=%s flags=0x%x iftype=%u inet=%s",
             when, n, name, i->ifa_addr->sa_family == AF_LINK ? "link" : "inet",
             (unsigned)i->ifa_flags, type, addr);
        n++;
    }
    freeifaddrs(list);
    emit("interfaces", "info", "when=%s listed=%u limit=24", when, n);
}
static int step_lo0(int s)
{
    short before = 0, after = 0;
    int had = has_inet("lo0", IPV4(127, 0, 0, 1)), e1 = 0, e2 = 0, e;
    e = if_flags(s, "lo0", &before);
    if (e) { result("lo0", 0, 1, "errno=%d stage=get_flags", e); return 0; }
    if (!had) e1 = if_add_inet(s, "lo0", IPV4(127, 0, 0, 1), IPV4(127, 0, 0, 1), IPV4(255, 0, 0, 0));
    if (!(before & IFF_UP)) e2 = if_up(s, "lo0");
    (void)if_flags(s, "lo0", &after);
    int ok = !e1 && !e2 && (after & IFF_UP) && has_inet("lo0", IPV4(127, 0, 0, 1));
    result("lo0", ok, 1, "was_up=%d had_127=%d add_errno=%d up_errno=%d now_up=%d",
           !!(before & IFF_UP), had, e1, e2, !!(after & IFF_UP));
    return ok;
}
static void step_udp_loopback(void)
{
    char pl[64], got[64];
    struct sockaddr_in a = sin4(IPV4(127, 0, 0, 1), 0), from;
    socklen_t al = sizeof a, fl = sizeof from;
    int fd = socket(AF_INET, SOCK_DGRAM, 0), ok = 0, e = 0;
    ssize_t n = -1;
    int len = snprintf(pl, sizeof pl, "DL25 udp %.*s", (int)DLN_TAG_BYTES, g_tag);
    if (fd < 0) { result("udp_loopback", 0, 1, "errno=%d stage=socket", errno); return; }
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || getsockname(fd, (struct sockaddr *)&a, &al) < 0 ||
        sendto(fd, pl, (size_t)len, 0, (struct sockaddr *)&a, sizeof a) != len) e = errno;
    else if (wait_readable(fd, now_ms() + WAIT_MS)) {
        n = recvfrom(fd, got, sizeof got, 0, (struct sockaddr *)&from, &fl);
        ok = n == len && memcmp(got, pl, (size_t)len) == 0;
    }
    close(fd);
    result("udp_loopback", ok, 1, "bytes=%zd errno=%d", n, e);
}
static void step_tcp_loopback(void)
{
    char pl[64], got[64], echo[64];
    struct sockaddr_in a = sin4(IPV4(127, 0, 0, 1), 0);
    socklen_t al = sizeof a;
    int l = -1, c = -1, x = -1, ok = 0, e = 0, one = 1;
    ssize_t n = -1, m = -1;
    int64_t dl = now_ms() + WAIT_MS;
    int len = snprintf(pl, sizeof pl, "DL25 tcp %.*s", (int)DLN_TAG_BYTES, g_tag);
    l = socket(AF_INET, SOCK_STREAM, 0);
    c = socket(AF_INET, SOCK_STREAM, 0);
    if (l < 0 || c < 0) { e = errno; goto out; }
    (void)setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(l, (struct sockaddr *)&a, sizeof a) < 0 || listen(l, 1) < 0 ||
        getsockname(l, (struct sockaddr *)&a, &al) < 0) { e = errno; goto out; }
    if (fcntl(c, F_SETFL, O_NONBLOCK) < 0) { e = errno; goto out; }
    if (connect(c, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) { e = errno; goto out; }
    if (!wait_readable(l, dl)) { e = ETIMEDOUT; goto out; }
    x = accept(l, NULL, NULL);
    if (x < 0) { e = errno; goto out; }
    if (fcntl(c, F_SETFL, 0) < 0) { e = errno; goto out; }
    if (send(c, pl, (size_t)len, 0) != len) { e = errno; goto out; }
    if (!wait_readable(x, dl)) { e = ETIMEDOUT; goto out; }
    n = recv(x, got, sizeof got, 0);
    if (n != len || memcmp(got, pl, (size_t)len)) goto out;
    if (send(x, got, (size_t)n, 0) != n) { e = errno; goto out; }
    if (!wait_readable(c, dl)) { e = ETIMEDOUT; goto out; }
    m = recv(c, echo, sizeof echo, 0);
    ok = m == len && memcmp(echo, pl, (size_t)len) == 0;
out:
    if (x >= 0) close(x);
    if (c >= 0) close(c);
    if (l >= 0) close(l);
    result("tcp_loopback", ok, 1, "sent=%d received=%zd echoed=%zd errno=%d", len, n, m, e);
}

/* One feth pair: create, peer, up, frames both ways, kernel ARP + IPv4 out. */
static void feth_pair(int s, const char *label, int required, uint32_t a_ip, uint32_t b_ip)
{
    char a[IFNAMSIZ] = "", b[IFNAMSIZ] = "", step[32];
    struct bpf_dev ba = {-1, 0, NULL}, bb = {-1, 0, NULL};
    struct link_info la = {0}, lb = {0}, la2 = {0}, lb2 = {0};
    uint8_t frame[DLN_ETH_MIN_FRAME];
    static const uint8_t bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    int e, r1 = 0, r2 = 0, ra = 0, ru = 0, us = -1;

    snprintf(step, sizeof step, "%s_create", label);
    e = if_create(s, "feth", a);
    if (!e) { e = if_create(s, "feth", b); if (e) { (void)if_destroy(s, a); a[0] = 0; } }
    result(step, !e, required, "a=%s b=%s errno=%d", a[0] ? a : "-", b[0] ? b : "-", e);
    if (e) return;

    snprintf(step, sizeof step, "%s_peer", label);
    e = feth_peer(s, a, b);
    result(step, !e, required, "errno=%d", e);
    if (e) goto destroy;

    snprintf(step, sizeof step, "%s_up", label);
    {
        int ea = if_up(s, a), eb = if_up(s, b);
        la = link_of(a); lb = link_of(b);
        result(step, !ea && !eb && la.found && lb.found, required,
               "a_errno=%d b_errno=%d a_type=%u b_type=%u", ea, eb, la.type, lb.type);
        if (ea || eb || !la.found || !lb.found) goto destroy;
    }

    snprintf(step, sizeof step, "%s_bpf", label);
    {
        int ea = bpf_open(a, &ba), eb = bpf_open(b, &bb);
        result(step, !ea && !eb, required, "a_errno=%d b_errno=%d blen=%zu", ea, eb, bb.blen);
        if (ea || eb) goto destroy;
    }

    /* Raw frames A->B and B->A. */
    {
        struct probe_want wb = {la.mac, 1, 1}, wa = {lb.mac, 2, 1};
        dln_probe_frame(frame, bcast, la.mac, 1, 1, g_tag);
        r1 = write(ba.fd, frame, sizeof frame) == (ssize_t)sizeof frame
                 ? bpf_wait(&bb, want_probe, &wb, now_ms() + WAIT_MS) : -errno;
        dln_probe_frame(frame, bcast, lb.mac, 2, 1, g_tag);
        r2 = write(bb.fd, frame, sizeof frame) == (ssize_t)sizeof frame
                 ? bpf_wait(&ba, want_probe, &wa, now_ms() + WAIT_MS) : -errno;
        la2 = link_of(a); lb2 = link_of(b);
        snprintf(step, sizeof step, "%s_frame_a2b", label);
        result(step, r1 == 1, required, "status=%d a_opackets_delta=%u b_ipackets_delta=%u", r1,
               la2.opackets - la.opackets, lb2.ipackets - lb.ipackets);
        snprintf(step, sizeof step, "%s_frame_b2a", label);
        result(step, r2 == 1, required, "status=%d b_opackets_delta=%u a_ipackets_delta=%u", r2,
               lb2.opackets - lb.opackets, la2.ipackets - la.ipackets);
    }

    /* Kernel ARP request from A, user-space reply from B, then the queued
     * UDP datagram must leave A for B's address. */
    {
        char pl[64];
        int len = snprintf(pl, sizeof pl, "DL25 ip %.*s", (int)DLN_TAG_BYTES, g_tag);
        struct sockaddr_in dst = sin4(b_ip, DISCARD_PORT);
        struct arp_want aw = {la.mac, a_ip, b_ip};
        struct udp_want uw = {la.mac, b_ip, DISCARD_PORT, (const uint8_t *)pl, (size_t)len};
        int ea = if_add_inet(s, a, a_ip, a_ip | 0xffu, IPV4(255, 255, 255, 0)), se = 0;
        snprintf(step, sizeof step, "%s_inet", label);
        result(step, !ea, required, "addr=%u.%u.%u.%u/24 errno=%d", a_ip >> 24, (a_ip >> 16) & 255,
               (a_ip >> 8) & 255, a_ip & 255, ea);
        if (ea) goto destroy;
        us = socket(AF_INET, SOCK_DGRAM, 0);
        if (us < 0 || sendto(us, pl, (size_t)len, 0, (struct sockaddr *)&dst, sizeof dst) != len)
            se = errno;
        ra = se ? -se : bpf_wait(&bb, want_arp, &aw, now_ms() + WAIT_MS);
        snprintf(step, sizeof step, "%s_arp_request", label);
        result(step, ra == 1, required, "status=%d send_errno=%d", ra, se);
        if (ra == 1) {
            dln_arp_reply(frame, lb.mac, b_ip, la.mac, a_ip);
            ru = write(bb.fd, frame, sizeof frame) == (ssize_t)sizeof frame
                     ? bpf_wait(&bb, want_udp, &uw, now_ms() + WAIT_MS) : -errno;
        }
        snprintf(step, sizeof step, "%s_ipv4_out", label);
        if (ra == 1) result(step, ru == 1, required, "status=%d bytes=%d", ru, len);
        else skip(step, "no_arp_request");
    }

destroy:
    if (us >= 0) close(us);
    bpf_close(&ba);
    bpf_close(&bb);
    {
        int ea = a[0] ? if_destroy(s, a) : 0, eb = b[0] ? if_destroy(s, b) : 0;
        snprintf(step, sizeof step, "%s_destroy", label);
        result(step, !ea && !eb, required, "a_errno=%d b_errno=%d", ea, eb);
    }
}

static void step_skywalk(int s)
{
    int old = 1, zero = 0, e;
    size_t len = sizeof old;
    if (sysctlbyname("net.link.fake.bsd_mode", &old, &len, &zero, sizeof zero) != 0) {
        e = errno;
        result("skywalk_mode", 0, 1, "errno=%d", e);
        return;
    }
    result("skywalk_mode", 1, 1, "bsd_mode_was=%d bsd_mode_now=0", old);
    feth_pair(s, "skywalk_feth", 1, SKY_A_IP, SKY_B_IP);
    e = sysctlbyname("net.link.fake.bsd_mode", NULL, NULL, &old, sizeof old) == 0 ? 0 : errno;
    result("skywalk_restore", !e, 1, "bsd_mode=%d errno=%d", old, e);
}

static void step_utun(int s)
{
    struct ctl_info ci;
    struct sockaddr_ctl sc;
    char name[IFNAMSIZ] = "", pl[64], buf[2048];
    socklen_t nl = sizeof name;
    int u, e = 0, out = 0, in = 0, us = -1, rs = -1;
    int len = snprintf(pl, sizeof pl, "DL25 utun %.*s", (int)DLN_TAG_BYTES, g_tag);

    u = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (u < 0) { result("utun_open", 0, 1, "errno=%d stage=socket", errno); return; }
    memset(&ci, 0, sizeof ci);
    strlcpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof ci.ctl_name);
    memset(&sc, 0, sizeof sc);
    if (ioctl(u, CTLIOCGINFO, &ci) < 0) e = errno;
    else {
        sc.sc_len = sizeof sc;
        sc.sc_family = AF_SYSTEM;
        sc.ss_sysaddr = AF_SYS_CONTROL;
        sc.sc_id = ci.ctl_id;
        sc.sc_unit = 0;                               /* kernel picks the unit */
        if (connect(u, (struct sockaddr *)&sc, sizeof sc) < 0 ||
            getsockopt(u, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, name, &nl) < 0) e = errno;
    }
    name[sizeof name - 1] = '\0';
    result("utun_open", !e && name[0], 1, "name=%s errno=%d", name[0] ? name : "-", e);
    if (e || !name[0]) { close(u); return; }

    e = if_add_inet(s, name, UTUN_LOCAL, UTUN_PEER, IPV4(255, 255, 255, 255));
    if (!e) e = if_up(s, name);
    result("utun_inet", !e, 1, "local=10.251.0.1 peer=10.251.0.2 errno=%d", e);
    if (e) goto done;

    /* Out: a kernel UDP send to the peer address must reach the utun fd. */
    {
        struct sockaddr_in dst = sin4(UTUN_PEER, DISCARD_PORT);
        int64_t dl = now_ms() + WAIT_MS;
        us = socket(AF_INET, SOCK_DGRAM, 0);
        if (us < 0 || sendto(us, pl, (size_t)len, 0, (struct sockaddr *)&dst, sizeof dst) != len)
            e = errno;
        while (!e && !out && wait_readable(u, dl)) {
            ssize_t n = read(u, buf, sizeof buf);
            uint32_t fam, src = 0, d = 0;
            uint16_t dp = 0;
            const uint8_t *p = NULL;
            size_t pn = 0;
            if (n < 4) continue;
            memcpy(&fam, buf, 4);
            if (ntohl(fam) != AF_INET) continue;
            if (dln_ipv4_udp_parse((const uint8_t *)buf + 4, (size_t)n - 4, &src, &d, NULL, &dp, &p, &pn) &&
                src == UTUN_LOCAL && d == UTUN_PEER && dp == DISCARD_PORT && pn == (size_t)len &&
                memcmp(p, pl, (size_t)len) == 0)
                out = 1;
        }
        result("utun_out", out, 1, "bytes=%d errno=%d", len, e);
        e = 0;
    }

    /* In: a packet written to the utun fd must reach a local UDP socket. */
    {
        struct sockaddr_in local = sin4(UTUN_LOCAL, 0), from;
        socklen_t ll = sizeof local, fl = sizeof from;
        uint8_t pkt[256];
        size_t n;
        uint32_t fam = htonl(AF_INET);
        rs = socket(AF_INET, SOCK_DGRAM, 0);
        if (rs < 0 || bind(rs, (struct sockaddr *)&local, sizeof local) < 0 ||
            getsockname(rs, (struct sockaddr *)&local, &ll) < 0) e = errno;
        else {
            char tagged[64], got[64];
            int tl = snprintf(tagged, sizeof tagged, "DL25 utun-in %.*s", (int)DLN_TAG_BYTES, g_tag);
            memcpy(pkt, &fam, 4);
            n = dln_ipv4_udp(pkt + 4, sizeof pkt - 4, UTUN_PEER, UTUN_LOCAL, DISCARD_PORT,
                             ntohs(local.sin_port), (const uint8_t *)tagged, (size_t)tl, 0x2525);
            if (!n || write(u, pkt, n + 4) != (ssize_t)(n + 4)) e = n ? errno : EINVAL;
            else if (wait_readable(rs, now_ms() + WAIT_MS)) {
                ssize_t g = recvfrom(rs, got, sizeof got, 0, (struct sockaddr *)&from, &fl);
                in = g == tl && memcmp(got, tagged, (size_t)tl) == 0 &&
                     ntohl(from.sin_addr.s_addr) == UTUN_PEER && ntohs(from.sin_port) == DISCARD_PORT;
            }
        }
        result("utun_in", in, 1, "errno=%d", e);
    }
done:
    if (us >= 0) close(us);
    if (rs >= 0) close(rs);
    close(u);   /* closing the control socket destroys the utun interface */
    result("utun_close", 1, 1, "name=%s", name);
}

int main(int argc, char **argv)
{
    int skywalk = 0, s, i;
    static const char none[] = "000000000000000000000000000000000000000000000000";
    g_nonce = none;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nonce") && i + 1 < argc) g_nonce = argv[++i];
        else if (!strcmp(argv[i], "--skywalk")) skywalk = 1;
        else { fprintf(stderr, "usage: darwin-net-probe --nonce <48 hex> [--skywalk]\n"); return 2; }
    }
    if (g_nonce == none || !dln_nonce_ok(g_nonce)) {
        fprintf(stderr, "usage: darwin-net-probe --nonce <48 hex> [--skywalk]\n");
        return 2;
    }
    memcpy(g_tag, g_nonce, DLN_TAG_BYTES);
    setvbuf(stdout, NULL, _IOLBF, 0);
    emit("begin", "info", "version=1 skywalk=%d wait_ms=%d", skywalk, WAIT_MS);
    step_identity();
    step_sysctls();
    step_interfaces("before");
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        result("control_socket", 0, 1, "errno=%d", errno);
    } else {
        if (step_lo0(s)) {
            step_udp_loopback();
            step_tcp_loopback();
        } else {
            skip("udp_loopback", "lo0_not_ready");
            skip("tcp_loopback", "lo0_not_ready");
        }
        feth_pair(s, "feth", 1, FETH_A_IP, FETH_B_IP);
        step_utun(s);
        if (skywalk) step_skywalk(s);
        else skip("skywalk_feth", "not_requested");
        step_interfaces("after");
        close(s);
    }
    emit("done", "info", "passed=%u failed=%u skipped=%u", g_passed, g_failed, g_skipped);
    return g_failed ? 1 : 0;
}
