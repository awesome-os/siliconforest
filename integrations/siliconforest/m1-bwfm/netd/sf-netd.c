/*
 * sf-netd - keeps Wi-Fi up on a SiliconForest root that has no configd.
 *
 * Copyright (c) 2026 darwin-lab. SPDX-License-Identifier: ISC
 *
 * sf-netd [--once] [--timeout S] [--poll S] [--renew-after S] [--lease-cap S] [--no-resolver]
 *         [--live-after S] [--live-target ADDR] [--wired] [IFNAME]
 *     One supervisor for one interface (default en0). It turns the radio on, and while the
 *     interface is not associated and was not left on purpose (wifiutil leave) it runs
 *     "wifiutil autojoin" with a growing pause between tries. Once associated it keeps
 *     "sf-dhcp --daemon" running for the interface and, when a lease is configured, the
 *     resolver daemon (mdnsd -debug). A child that dies is started again. Each change of state
 *     is one line "sf-netd: IFNAME state=..." on stdout and in /var/run/sf-netd.IFNAME.status.
 *     Every line on stdout begins with the time (UTC, "2026-11-17T17:20:01Z "), the lines of
 *     sf-dhcp and wifiutil included: their output comes through a pipe and is written out line
 *     by line with the time it arrived. The status file has none. A second instance for the same interface exits. SIGTERM stops the children it
 *     started.
 *
 *     Liveness: while it reports online it sends the lease's router an ICMP echo at every poll.
 *     When no reply has come for --live-after seconds (default 0: off; S seconds turns the check on) it
 *     says so, drops the association without the "left" marker, stops sf-dhcp and joins again
 *     as after any lost association. A router that stays silent is tried again after twice the
 *     time, up to ten minutes; the first reply resets that.
 *
 *     --once brings the network up once (join, one-shot sf-dhcp, resolver) and exits 0, or 1
 *     when that did not work within --timeout seconds (default 90); the resolver stays running.
 *     --renew-after and --lease-cap are handed to sf-dhcp (its test hooks).
 *     Test hooks of the liveness check: --live-target ADDR sends the echo there instead of to
 *     the router; --wired takes an interface without the 802.11 requests (QEMU's virtio
 *     Ethernet) as associated, and a re-join there only starts sf-dhcp again.
 *
 * sf-netd --drop-link [IFNAME]
 *     Test hook: disassociates without leaving the "left" marker, so autojoin still applies.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * The 802.11 requests wifiutil uses (SiliconForest IO80211Family/apple80211_ioctl.h, which is not
 * in the SDK); the few structures needed here are repeated.
 */
struct a80211req {
    char req_if_name[16];
    int req_type;
    int req_val;
    uint32_t req_len;
    void *req_data;
};
struct a80211_u32 {             /* apple80211_state_data and apple80211_power_data */
    uint32_t version;
    uint32_t value;
};
struct a80211_ssid {
    uint32_t version;
    uint32_t ssid_len;
    uint8_t ssid_bytes[32];
};
#define SIOCSA80211 _IOW('i', 200, struct a80211req)
#define SIOCGA80211 _IOWR('i', 201, struct a80211req)
#define A80211_IOC_SSID 1
#define A80211_IOC_STATE 13
#define A80211_IOC_POWER 19
#define A80211_IOC_DISASSOCIATE 22
#define A80211_S_AUTH 2
#define A80211_S_ASSOC 3
#define A80211_S_RUN 4

/* sfwifi.c: left by "wifiutil leave", so autojoin waits for the next join */
#define LEFT_PATH "/var/run/org.siliconforest.wifi.left"
/* sfwifi.c: held while a join runs; it counts for 60 seconds */
#define JOINING_PATH "/var/run/org.siliconforest.wifi.joining"
#define RESOLVER_SOCKET "/var/run/mDNSResponder"
#define RESOLVER_LOG "/var/log/mdnsd.log"

enum nstate { N_START, N_NO_INTERFACE, N_RADIO_OFF, N_LEFT, N_JOINING, N_ASSOCIATED, N_ONLINE, N_COUNT };
static const char *const nnames[N_COUNT] = { "starting", "no-interface", "radio-off", "left", "joining", "associated", "online" };

struct ctx {
    const char *ifname;
    int once, timeout_s, poll_s, no_resolver;
    const char *renew_after, *lease_cap;
    const char *wifiutil, *sf_dhcp, *mdnsd;

    int lockfd;
    char lockpath[64], pidpath[64], statuspath[64], leasepath[64];

    enum nstate state;
    char detail[200];

    pid_t dhcp, mdns, join;     /* children, 0 when not running */
    int64_t dhcp_started, mdns_started, join_started;
    int mdns_external;          /* someone else's resolver answered on the socket */
    char mdns_dns[16];          /* the name server our mdnsd was started with */
    int powered;                /* the radio was turned on once; later an off is the user's */
    int power_tries;
    int was_associated;
    int backoff;                /* seconds between autojoin tries */
    int64_t next_join;
    int once_dhcp_done;

    int wired;                  /* --wired: no 802.11 requests, the interface counts as associated */
    int live_after;             /* seconds without an echo reply before a re-join; 0: no check */
    const char *live_target;    /* --live-target, NULL: the lease's router */
    int live_fd;                /* the ICMP socket, -1 when there is none */
    uint32_t live_addr;         /* whom the echoes go to now (network order), 0: nobody */
    int64_t live_since;         /* the last reply, or when the watch of live_addr began */
    int live_wait;              /* the silence that triggers the next re-join (grows while it stays silent) */
    uint16_t live_seq;
    unsigned live_sent, live_got, live_rejoins;
};

static volatile sig_atomic_t stopping;

static void on_stop(int sig)
{
    (void)sig;
    stopping = 1;
}

static void on_child(int sig)
{
    (void)sig;                  /* only to interrupt the poll */
}

static int64_t now_s(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0)
        return (int64_t)ts.tv_sec;
    return (int64_t)time(NULL);
}

/* The time in front of every log line: wall clock, UTC. */
static void stamp(FILE *f)
{
    time_t t = time(NULL);
    struct tm tm;
    char buf[24];

    if (gmtime_r(&t, &tm) != NULL && strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) > 0)
        fprintf(f, "%s ", buf);
}

static void say(const struct ctx *c, const char *fmt, ...)
{
    va_list ap;

    stamp(stdout);
    printf("sf-netd: %s ", c->ifname);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ---- the children's output ---------------------------------------------------------------- */

/* What sf-dhcp and wifiutil print arrives here and leaves with the time in front of each line. */
static int outpipe[2] = { -1, -1 };
static char outbuf[1024];
static size_t outlen;

static void out_line(const char *p, size_t n)
{
    stamp(stdout);
    fwrite(p, 1, n, stdout);
    fputc('\n', stdout);
}

static void drain_children(int final)
{
    ssize_t n;

    if (outpipe[0] < 0)
        return;
    while ((n = read(outpipe[0], outbuf + outlen, sizeof(outbuf) - outlen)) > 0) {
        size_t start = 0;

        outlen += (size_t)n;
        for (size_t i = 0; i < outlen; i++) {
            if (outbuf[i] == '\n') {
                out_line(outbuf + start, i - start);
                start = i + 1;
            }
        }
        if (start == 0 && outlen == sizeof(outbuf)) {   /* a line longer than the buffer goes out in pieces */
            out_line(outbuf, outlen);
            start = outlen;
        }
        memmove(outbuf, outbuf + start, outlen - start);
        outlen -= start;
    }
    if (final && outlen > 0) {
        out_line(outbuf, outlen);
        outlen = 0;
    }
}

static void open_outpipe(void)
{
    if (pipe(outpipe) != 0) {
        outpipe[0] = outpipe[1] = -1;       /* the children then write to stdout themselves, without the time */
        return;
    }
    for (int i = 0; i < 2; i++)
        fcntl(outpipe[i], F_SETFD, FD_CLOEXEC);
    fcntl(outpipe[0], F_SETFL, fcntl(outpipe[0], F_GETFL) | O_NONBLOCK);
}

/* ---- the interface ---------------------------------------------------------------------- */

static int a80211(const struct ctx *c, int set, int type, void *data, uint32_t len)
{
    struct a80211req req;
    int s = socket(AF_INET, SOCK_DGRAM, 0), rc;

    if (s < 0)
        return -1;
    memset(&req, 0, sizeof(req));
    strlcpy(req.req_if_name, c->ifname, sizeof(req.req_if_name));
    req.req_type = type;
    req.req_len = len;
    req.req_data = data;
    rc = ioctl(s, set ? SIOCSA80211 : SIOCGA80211, &req);
    close(s);
    return rc;
}

/* 0 init, 1 scanning, 2 authenticating, 3 associating, 4 running; -1 when there is no such interface */
static int wifi_state(const struct ctx *c)
{
    struct a80211_u32 st = { 1, 0 };

    if (c->wired)
        return A80211_S_RUN;
    if (a80211(c, 0, A80211_IOC_STATE, &st, sizeof(st)) < 0)
        return -1;
    return (int)st.value;
}

static int wifi_power(const struct ctx *c)
{
    struct a80211_u32 p = { 1, 0 };

    if (c->wired)
        return 1;
    if (a80211(c, 0, A80211_IOC_POWER, &p, sizeof(p)) < 0)
        return 0;
    return p.value != 0;
}

static void wifi_ssid(const struct ctx *c, char out[33])
{
    struct a80211_ssid s;
    uint32_t n;

    memset(&s, 0, sizeof(s));
    out[0] = 0;
    if (a80211(c, 0, A80211_IOC_SSID, &s, sizeof(s)) < 0)
        return;
    n = s.ssid_len > 32 ? 32 : s.ssid_len;
    for (uint32_t i = 0; i < n; i++)
        out[i] = s.ssid_bytes[i] > 0x20 && s.ssid_bytes[i] < 0x7f ? (char)s.ssid_bytes[i] : '_';
    out[n] = 0;
}

static void if_up(const struct ctx *c)
{
    struct ifreq ifr;
    int s = socket(AF_INET, SOCK_DGRAM, 0);

    if (s < 0)
        return;
    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, c->ifname, sizeof(ifr.ifr_name));
    if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0 && !(ifr.ifr_flags & IFF_UP)) {
        ifr.ifr_flags |= IFF_UP;
        ioctl(s, SIOCSIFFLAGS, &ifr);
    }
    close(s);
}

/* ---- children --------------------------------------------------------------------------- */

static const char *find_tool(const char *name)
{
    static const char *const dirs[] = { "/usr/sbin", "/usr/bin", "/sbin", "/bin" };
    char path[64];

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dirs[i], name);
        if (access(path, X_OK) == 0)
            return strdup(path);
    }
    return NULL;
}

/* Starts argv; output goes to `log` when given. A detached child is in its own session. */
static pid_t spawn(const struct ctx *c, const char *const argv[], const char *log, int detach)
{
    pid_t pid;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid != 0)
        return pid;
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    if (c->lockfd >= 0)
        close(c->lockfd);
    if (detach)
        setsid();
    if (log != NULL) {
        int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2)
                close(fd);
        }
    } else if (outpipe[1] >= 0) {
        dup2(outpipe[1], 1);
        dup2(outpipe[1], 2);
    }
    execv(argv[0], (char *const *)argv);
    stamp(stderr);
    fprintf(stderr, "sf-netd: exec %s: %s\n", argv[0], strerror(errno));
    _exit(127);
}

static int exit_code(int status)
{
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

/* Runs argv to its end, at most `seconds`. Returns its exit code, -1 when cut short. */
static int run_wait(const struct ctx *c, const char *const argv[], int seconds)
{
    int64_t end = now_s() + seconds;
    int status;
    pid_t pid = spawn(c, argv, NULL, 0);

    if (pid < 0)
        return -1;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);

        if (r == pid)
            return exit_code(status);
        if (r < 0 && errno != EINTR)
            return -1;
        drain_children(0);
        if (stopping || now_s() > end) {
            kill(pid, SIGTERM);
            usleep(200000);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }
        usleep(100000);
    }
}

static int resolver_answers(void)
{
    struct sockaddr_un sun;
    int s = socket(AF_UNIX, SOCK_STREAM, 0), ok;

    if (s < 0)
        return 0;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strlcpy(sun.sun_path, RESOLVER_SOCKET, sizeof(sun.sun_path));
    ok = connect(s, (struct sockaddr *)&sun, sizeof(sun)) == 0;
    close(s);
    return ok;
}

static int resolver_socket_exists(void)
{
    struct stat st;

    return stat(RESOLVER_SOCKET, &st) == 0 && S_ISSOCK(st.st_mode);
}

/* The children that ended since the last look. */
static void reap(struct ctx *c)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == c->dhcp) {
            c->dhcp = 0;
            if (!stopping)
                say(c, "sf-dhcp (pid %d) ended with %d", (int)pid, exit_code(status));
        } else if (pid == c->mdns) {
            c->mdns = 0;
            if (!stopping)
                say(c, "resolver (pid %d) ended with %d", (int)pid, exit_code(status));
        } else if (pid == c->join) {
            int rc = exit_code(status);

            c->join = 0;
            if (rc != 0 || wifi_state(c) != A80211_S_RUN) {
                c->backoff = c->backoff == 0 ? 5 : c->backoff * 2;
                if (c->backoff > 60)
                    c->backoff = 60;
                c->next_join = now_s() + c->backoff;
                if (!stopping)
                    say(c, "autojoin ended with %d, next try in %ds", rc, c->backoff);
            }
        }
    }
}

/* ---- state ------------------------------------------------------------------------------ */

static void set_state(struct ctx *c, enum nstate st, const char *fmt, ...)
{
    char detail[sizeof(c->detail)];
    va_list ap;
    FILE *f;

    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    if (st == c->state && strcmp(detail, c->detail) == 0)
        return;
    c->state = st;
    strlcpy(c->detail, detail, sizeof(c->detail));
    say(c, "state=%s%s%s", nnames[st], detail[0] ? " " : "", detail);
    f = fopen(c->statuspath, "w");
    if (f != NULL) {
        fprintf(f, "state=%s%s%s\n", nnames[st], detail[0] ? " " : "", detail);
        fclose(f);
    }
}

/* The line sf-dhcp keeps while it holds a configured lease: "addr=... mask=... dns=... ..." */
static int read_lease(const struct ctx *c, char *line, size_t size, char dns[16])
{
    FILE *f = fopen(c->leasepath, "r");
    char *p;

    line[0] = 0;
    dns[0] = 0;
    if (f == NULL)
        return 0;
    if (fgets(line, (int)size, f) == NULL)
        line[0] = 0;
    fclose(f);
    line[strcspn(line, "\n")] = 0;
    if ((p = strstr(line, "dns=")) != NULL) {
        size_t n = strcspn(p + 4, " ");

        if (n > 15)
            n = 15;
        memcpy(dns, p + 4, n);
        dns[n] = 0;
    }
    return line[0] != 0;
}

static int marker_fresh(const char *path, int seconds)
{
    struct stat st;

    return stat(path, &st) == 0 && time(NULL) - st.st_mtime < seconds;
}

static void ensure_dhcp(struct ctx *c)
{
    const char *argv[10];
    int n = 0;

    if (c->dhcp > 0 || now_s() - c->dhcp_started < 5)
        return;
    argv[n++] = c->sf_dhcp;
    argv[n++] = "--daemon";
    argv[n++] = "--exit-with-parent";
    if (c->renew_after != NULL) {
        argv[n++] = "--renew-after";
        argv[n++] = c->renew_after;
    }
    if (c->lease_cap != NULL) {
        argv[n++] = "--lease-cap";
        argv[n++] = c->lease_cap;
    }
    argv[n++] = c->ifname;
    argv[n] = NULL;
    unlink(c->leasepath);
    c->dhcp_started = now_s();
    c->dhcp = spawn(c, argv, NULL, 0);
    if (c->dhcp < 0) {
        c->dhcp = 0;
        say(c, "cannot start sf-dhcp: %s", strerror(errno));
    } else {
        say(c, "started sf-dhcp --daemon (pid %d)", (int)c->dhcp);
    }
}

/* Returns 1 when name lookups have a daemon to talk to. */
static int ensure_resolver(struct ctx *c, const char *dns)
{
    const char *argv[] = { c->mdnsd, "-debug", NULL };

    if (c->no_resolver)
        return 1;
    if (c->mdns > 0) {
        /* mdnsd reads /etc/resolv.conf when it starts; ours is started again for a new server */
        if (dns[0] && c->mdns_dns[0] && strcmp(dns, c->mdns_dns) != 0) {
            say(c, "name server changed from %s to %s, restarting the resolver", c->mdns_dns, dns);
            kill(c->mdns, SIGTERM);
            c->mdns_dns[0] = 0;
            return 0;
        }
        return resolver_socket_exists();
    }
    if (resolver_answers()) {
        if (!c->mdns_external)
            say(c, "a resolver is already running, leaving it alone");
        c->mdns_external = 1;
        return 1;
    }
    c->mdns_external = 0;
    if (c->mdnsd == NULL || now_s() - c->mdns_started < 5)
        return 0;
    /* -debug keeps mdnsd in the foreground; a socket nobody answers on is a dead daemon's */
    unlink(RESOLVER_SOCKET);
    c->mdns_started = now_s();
    c->mdns = spawn(c, argv, RESOLVER_LOG, c->once);
    if (c->mdns < 0) {
        c->mdns = 0;
        say(c, "cannot start the resolver: %s", strerror(errno));
        return 0;
    }
    strlcpy(c->mdns_dns, dns, sizeof(c->mdns_dns));
    say(c, "started resolver %s -debug (pid %d)", c->mdnsd, (int)c->mdns);
    return 0;
}

static void ensure_join(struct ctx *c, int st)
{
    const char *argv[] = { c->wifiutil, "-i", c->ifname, "autojoin", NULL };

    if (c->join > 0) {
        /* a scan and one 45 second join per remembered network in range; this one is stuck */
        if (now_s() - c->join_started > 150) {
            say(c, "autojoin (pid %d) is stuck, stopping it", (int)c->join);
            kill(c->join, SIGKILL);
            unlink(JOINING_PATH);
        }
        return;
    }
    /* the driver is in the middle of a join of its own, a scan now would retune the radio */
    if (st == A80211_S_AUTH || st == A80211_S_ASSOC)
        return;
    /* somebody else's join (wifiutil join, the desktop) is running */
    if (marker_fresh(JOINING_PATH, 60))
        return;
    if (now_s() < c->next_join)
        return;
    c->join_started = now_s();
    c->join = spawn(c, argv, NULL, 0);
    if (c->join < 0) {
        c->join = 0;
        c->next_join = now_s() + 10;
        say(c, "cannot start wifiutil: %s", strerror(errno));
    } else {
        say(c, "started wifiutil autojoin (pid %d)", (int)c->join);
    }
}

/* ---- liveness ---------------------------------------------------------------------------- */

static uint16_t icmp_sum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;

    for (; n > 1; p += 2, n -= 2)
        sum += (uint32_t)(p[0] << 8 | p[1]);
    if (n)
        sum += (uint32_t)(p[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static void live_forget(struct ctx *c)
{
    c->live_addr = 0;
}

/* "router=a.b.c.d" of the lease line, or --live-target; 0 when there is none */
static uint32_t live_whom(const struct ctx *c, const char *lease)
{
    char text[16];
    const char *p = c->live_target;
    struct in_addr a;
    size_t n;

    if (p == NULL) {
        if ((p = strstr(lease, "router=")) == NULL)
            return 0;
        p += 7;
    }
    n = strcspn(p, " ");
    if (n == 0 || n > 15)
        return 0;
    memcpy(text, p, n);
    text[n] = 0;
    if (inet_pton(AF_INET, text, &a) != 1 || a.s_addr == 0)
        return 0;
    return a.s_addr;
}

/*
 * Called at every poll while online. Takes the echo replies that came, sends the next echo.
 * Returns 1 when the router has been silent for too long and a re-join is due.
 */
static int live_check(struct ctx *c, const char *lease)
{
    uint32_t whom = c->live_after > 0 ? live_whom(c, lease) : 0;
    uint8_t buf[256], pkt[16];
    struct sockaddr_in from, to;
    socklen_t flen;
    ssize_t n;
    int64_t now = now_s();

    if (whom == 0) {
        live_forget(c);
        return 0;
    }
    if (c->live_fd < 0) {
        /* a datagram ICMP socket needs no privilege; a raw one is the same wire format */
        c->live_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (c->live_fd < 0)
            c->live_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (c->live_fd < 0) {
            say(c, "no ICMP socket (%s), the router is not watched", strerror(errno));
            c->live_after = 0;
            return 0;
        }
        fcntl(c->live_fd, F_SETFL, fcntl(c->live_fd, F_GETFL) | O_NONBLOCK);
        fcntl(c->live_fd, F_SETFD, FD_CLOEXEC);
    }
    if (whom != c->live_addr) {
        /* online again, or another router: the count of silence begins now */
        c->live_addr = whom;
        c->live_since = now;
        if (c->live_wait < c->live_after)
            c->live_wait = c->live_after;
    }
    for (;;) {
        const uint8_t *p = buf;

        flen = sizeof(from);
        n = recvfrom(c->live_fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n <= 0)
            break;
        /* both kinds of socket hand over the IP header on this system; take it off when it is there */
        if (n >= 20 && (p[0] >> 4) == 4) {
            size_t ihl = (size_t)(p[0] & 15) * 4;

            if ((size_t)n < ihl + 8)
                continue;
            p += ihl;
            n -= (ssize_t)ihl;
        }
        if (n >= 8 && p[0] == ICMP_ECHOREPLY && from.sin_addr.s_addr == c->live_addr) {
            c->live_got++;
            c->live_since = now;
            if (c->live_wait != c->live_after) {
                say(c, "the router answers again");
                c->live_wait = c->live_after;
            }
        }
    }
    if (now - c->live_since >= c->live_wait)
        return 1;
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = ICMP_ECHO;
    pkt[4] = (uint8_t)(getpid() >> 8);
    pkt[5] = (uint8_t)getpid();
    c->live_seq++;
    pkt[6] = (uint8_t)(c->live_seq >> 8);
    pkt[7] = (uint8_t)c->live_seq;
    memcpy(pkt + 8, "sf-netd", 8);
    n = icmp_sum(pkt, sizeof(pkt));
    pkt[2] = (uint8_t)(n >> 8);
    pkt[3] = (uint8_t)n;
    memset(&to, 0, sizeof(to));
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = c->live_addr;
    /* an echo that cannot be sent (no route, interface down) is an echo without a reply */
    if (sendto(c->live_fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&to, sizeof(to)) == (ssize_t)sizeof(pkt))
        c->live_sent++;
    return 0;
}

/* The router did not answer: leave the network the way a lost association does and join again. */
static void live_rejoin(struct ctx *c)
{
    char text[16] = "?";
    struct in_addr a;

    a.s_addr = c->live_addr;
    inet_ntop(AF_INET, &a, text, sizeof(text));
    c->live_rejoins++;
    say(c, "router %s unreachable for %ds while online (echoes sent %u, replies %u), re-joining (%u)", text,
        (int)(now_s() - c->live_since), c->live_sent, c->live_got, c->live_rejoins);
    /* a router that stays silent is not asked for a new association every 45 seconds */
    c->live_wait = c->live_wait * 2 > 600 ? 600 : c->live_wait * 2;
    live_forget(c);
    if (c->dhcp > 0)
        kill(c->dhcp, SIGTERM);         /* reaped at the next look; it is started again once associated */
    unlink(c->leasepath);
    if (!c->wired) {
        if (a80211(c, 1, A80211_IOC_DISASSOCIATE, NULL, 0) < 0)
            say(c, "disassociate: %s", strerror(errno));
        c->was_associated = 0;
        c->backoff = 0;
        c->next_join = now_s();
    }
}

/* One look at the interface and the children. Returns 0 to go on, 1 done (--once), 2 failed (--once). */
static int tick(struct ctx *c)
{
    char lease[160], dns[16], ssid[33];
    int st;

    reap(c);
    if (c->wired)
        if_up(c);
    st = wifi_state(c);
    if (st < 0) {
        live_forget(c);
        set_state(c, N_NO_INTERFACE, "");
        return 0;
    }
    if (!wifi_power(c)) {
        if (!c->powered && c->power_tries < 5) {
            const char *argv[] = { c->wifiutil, "-i", c->ifname, "on", NULL };
            int rc;

            c->power_tries++;
            /* marking the interface up starts the radio as well; wifiutil is asked if it did not */
            if_up(c);
            rc = wifi_power(c) ? 0 : run_wait(c, argv, 20);
            say(c, "radio on, wifiutil returned %d", rc);
            if (wifi_power(c))
                c->powered = 1;
            return 0;
        }
        c->was_associated = 0;
        live_forget(c);
        /* turned on again, the driver rejoins the network it had by itself; give it a moment */
        c->next_join = now_s() + 6;
        c->backoff = 0;
        set_state(c, N_RADIO_OFF, c->powered ? "turned off, waiting for wifiutil on" : "the radio does not turn on");
        return c->once ? 2 : 0;
    }
    c->powered = 1;

    if (st != A80211_S_RUN) {
        live_forget(c);
        if (c->was_associated) {
            c->was_associated = 0;
            /* the driver rejoins by itself when it still has the network, give it a moment */
            c->next_join = now_s() + 6;
            c->backoff = 0;
            say(c, "association lost");
        }
        if (access(LEFT_PATH, F_OK) == 0) {
            set_state(c, N_LEFT, "left on purpose, waiting for a join");
            return c->once ? 2 : 0;
        }
        set_state(c, N_JOINING, "");
        ensure_join(c, st);
        return 0;
    }

    c->was_associated = 1;
    c->backoff = 0;
    if (c->wired)
        strlcpy(ssid, "(wired)", sizeof(ssid));
    else
        wifi_ssid(c, ssid);
    if (c->once) {
        if (!c->once_dhcp_done) {
            const char *argv[] = { c->sf_dhcp, c->ifname, NULL };
            int rc;

            set_state(c, N_ASSOCIATED, "ssid=%s", ssid);
            rc = run_wait(c, argv, 60);
            if (rc != 0) {
                say(c, "sf-dhcp returned %d", rc);
                return 2;
            }
            c->once_dhcp_done = 1;
        }
        if (!ensure_resolver(c, "")) {
            set_state(c, N_ASSOCIATED, "ssid=%s waiting for the resolver", ssid);
            return 0;
        }
        set_state(c, N_ONLINE, "ssid=%s", ssid);
        return 1;
    }

    ensure_dhcp(c);
    if (!read_lease(c, lease, sizeof(lease), dns)) {
        live_forget(c);
        set_state(c, N_ASSOCIATED, "ssid=%s waiting for a lease", ssid);
        return 0;
    }
    if (!ensure_resolver(c, dns)) {
        live_forget(c);
        set_state(c, N_ASSOCIATED, "ssid=%s %s waiting for the resolver", ssid, lease);
        return 0;
    }
    set_state(c, N_ONLINE, "ssid=%s %s", ssid, lease);
    if (live_check(c, lease)) {
        live_rejoin(c);
        set_state(c, c->wired ? N_ASSOCIATED : N_JOINING, c->wired ? "waiting for a lease" : "");
    }
    return 0;
}

/* ---- one instance ----------------------------------------------------------------------- */

/*
 * A listening socket in /var/run is the lock: a second instance connects to it and leaves. A
 * socket file nobody listens on belonged to an instance that died. Returns 0 with the lock (or
 * without one, when /var/run cannot hold it), 1 when another instance is running.
 */
static int take_lock(struct ctx *c)
{
    struct sockaddr_un sun;
    int err = 0;

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strlcpy(sun.sun_path, c->lockpath, sizeof(sun.sun_path));
    for (int pass = 0; pass < 2; pass++) {
        int s = socket(AF_UNIX, SOCK_STREAM, 0), probe;

        if (s < 0)
            break;
        if (bind(s, (struct sockaddr *)&sun, sizeof(sun)) == 0 && listen(s, 8) == 0) {
            fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
            fcntl(s, F_SETFD, FD_CLOEXEC);
            c->lockfd = s;
            return 0;
        }
        err = errno;
        close(s);
        if (err != EADDRINUSE)
            break;
        probe = socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe >= 0 && connect(probe, (struct sockaddr *)&sun, sizeof(sun)) == 0) {
            close(probe);
            return 1;
        }
        if (probe >= 0)
            close(probe);
        unlink(c->lockpath);
    }
    say(c, "no lock at %s (%s), running without", c->lockpath, strerror(err));
    c->lockfd = -1;
    return 0;
}

/* Waits one poll interval; connections of would-be second instances are taken and dropped. */
static void pause_tick(struct ctx *c)
{
    int64_t end = now_s() + c->poll_s, left;

    /* a signal ends the wait early; a child's output or a second instance does not */
    while (!stopping && (left = end - now_s()) > 0) {
        struct pollfd pfd[2] = { { c->lockfd, POLLIN, 0 }, { outpipe[0], POLLIN, 0 } };   /* poll passes over a -1 */

        if (poll(pfd, 2, (int)left * 1000) <= 0)
            break;
        if (pfd[1].revents)
            drain_children(0);
        if (pfd[0].revents) {
            int fd;

            while ((fd = accept(c->lockfd, NULL, NULL)) >= 0)
                close(fd);
            /* a socket that reports anything but a connection would make this loop spin */
            if (pfd[0].revents & ~POLLIN) {
                close(c->lockfd);
                c->lockfd = -1;
            }
        }
    }
    drain_children(0);
}

static void stop_children(struct ctx *c, int keep_resolver)
{
    pid_t *kids[] = { &c->join, &c->dhcp, &c->mdns };
    int killed_join = c->join > 0, killed_resolver, n = 0;

    if (keep_resolver)
        c->mdns = 0;
    killed_resolver = c->mdns > 0;
    for (int i = 0; i < 3; i++) {
        if (*kids[i] > 0) {
            kill(*kids[i], SIGTERM);
            n++;
        }
    }
    for (int waited = 0; (c->join > 0 || c->dhcp > 0 || c->mdns > 0) && waited < 50; waited++) {
        int status;

        for (int i = 0; i < 3; i++) {
            if (*kids[i] > 0 && waitpid(*kids[i], &status, WNOHANG) != 0)
                *kids[i] = 0;
        }
        if (waited == 30) {
            for (int i = 0; i < 3; i++) {
                if (*kids[i] > 0)
                    kill(*kids[i], SIGKILL);
            }
        }
        usleep(100000);
    }
    /* wifiutil does not take its marker back when it is stopped in the middle of a join */
    if (killed_join)
        unlink(JOINING_PATH);
    /* nor may a socket without a daemon stay, a lookup would wait on it */
    if (killed_resolver)
        unlink(RESOLVER_SOCKET);
    if (!c->once)
        unlink(c->leasepath);
    if (n > 0)
        say(c, "stopped %d child%s", n, n == 1 ? "" : "ren");
}

static void usage(void)
{
    fprintf(stderr, "usage: sf-netd [--once] [--timeout S] [--poll S] [--renew-after S] [--lease-cap S] [--no-resolver]\n"
                    "               [--live-after S] [--live-target ADDR] [--wired] [IFNAME]\n"
                    "       sf-netd --drop-link [IFNAME]\n");
}

int main(int argc, char **argv)
{
    static struct ctx c;
    struct sigaction sa;
    int64_t deadline;
    int i, drop = 0, rc = 0;
    FILE *f;

    c.ifname = "en0";
    c.timeout_s = 90;
    c.poll_s = 3;
    c.lockfd = -1;
    c.live_fd = -1;
    c.live_after = 0;   /* off by default: on the M1 (17 November 2026) a second device answered ARP for the router, the echoes went nowhere and the check re-joined once a minute, resetting every connection. Ask for it with --live-after S */
    for (i = 1; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
        if (strcmp(argv[i], "--once") == 0)
            c.once = 1;
        else if (strcmp(argv[i], "--no-resolver") == 0)
            c.no_resolver = 1;
        else if (strcmp(argv[i], "--drop-link") == 0)
            drop = 1;
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
            c.timeout_s = atoi(argv[++i]);
        else if (strcmp(argv[i], "--poll") == 0 && i + 1 < argc)
            c.poll_s = atoi(argv[++i]);
        else if (strcmp(argv[i], "--renew-after") == 0 && i + 1 < argc)
            c.renew_after = argv[++i];
        else if (strcmp(argv[i], "--lease-cap") == 0 && i + 1 < argc)
            c.lease_cap = argv[++i];
        else if (strcmp(argv[i], "--live-after") == 0 && i + 1 < argc)
            c.live_after = atoi(argv[++i]);
        else if (strcmp(argv[i], "--live-target") == 0 && i + 1 < argc)
            c.live_target = argv[++i];
        else if (strcmp(argv[i], "--wired") == 0)
            c.wired = 1;
        else {
            usage();
            return 2;
        }
    }
    if (i < argc)
        c.ifname = argv[i];
    if (strlen(c.ifname) > 15 || strchr(c.ifname, '/') != NULL) {
        usage();
        return 2;
    }
    if (c.poll_s < 2)
        c.poll_s = 2;
    if (c.poll_s > 5)
        c.poll_s = 5;
    /* less than three polls of silence is no evidence */
    if (c.live_after > 0 && c.live_after < 3 * c.poll_s)
        c.live_after = 3 * c.poll_s;
    c.live_wait = c.live_after;
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (drop) {
        if (a80211(&c, 1, A80211_IOC_DISASSOCIATE, NULL, 0) < 0) {
            say(&c, "disassociate: %s", strerror(errno));
            return 1;
        }
        say(&c, "disassociated, no left marker");
        return 0;
    }

    mkdir("/var", 0755);
    mkdir("/var/run", 0755);
    mkdir("/var/log", 0755);
    snprintf(c.lockpath, sizeof(c.lockpath), "/var/run/sf-netd.%s.lock", c.ifname);
    snprintf(c.pidpath, sizeof(c.pidpath), "/var/run/sf-netd.%s.pid", c.ifname);
    snprintf(c.statuspath, sizeof(c.statuspath), "/var/run/sf-netd.%s.status", c.ifname);
    snprintf(c.leasepath, sizeof(c.leasepath), "/var/run/sf-dhcp.%s.lease", c.ifname);

    if (take_lock(&c) != 0) {
        char line[240] = "";

        if ((f = fopen(c.statuspath, "r")) != NULL) {
            if (fgets(line, sizeof(line), f) == NULL)
                line[0] = 0;
            fclose(f);
            line[strcspn(line, "\n")] = 0;
        }
        say(&c, "already running, %s", line[0] ? line : "no status yet");
        /* for --once the question is whether the network is up, and the running instance knows */
        return c.once && strncmp(line, "state=online", 12) != 0 ? 1 : 0;
    }

    c.wifiutil = find_tool("wifiutil");
    c.sf_dhcp = find_tool("sf-dhcp");
    c.mdnsd = find_tool("mdnsd");
    if ((c.wifiutil == NULL && !c.wired) || c.sf_dhcp == NULL || (c.mdnsd == NULL && !c.no_resolver)) {
        say(&c, "missing %s", c.wifiutil == NULL ? "wifiutil" : c.sf_dhcp == NULL ? "sf-dhcp" : "mdnsd");
        if (c.lockfd >= 0)
            unlink(c.lockpath);
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop;            /* no SA_RESTART: the poll returns */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = on_child;
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if ((f = fopen(c.pidpath, "w")) != NULL) {
        fprintf(f, "%d\n", (int)getpid());
        fclose(f);
    }
    open_outpipe();
    c.state = N_START;
    say(&c, "pid %d%s%s", (int)getpid(), c.once ? " once" : "", c.wired ? " wired" : "");
    deadline = now_s() + c.timeout_s;

    while (!stopping) {
        rc = tick(&c);
        if (rc != 0)
            break;
        if (c.once && now_s() > deadline) {
            say(&c, "not up after %d seconds", c.timeout_s);
            rc = 2;
            break;
        }
        pause_tick(&c);
    }

    /* --once that worked leaves the resolver behind; everything else is taken down */
    stop_children(&c, c.once && rc == 1);
    drain_children(1);
    if (!c.once)
        say(&c, "stopped");
    unlink(c.statuspath);
    unlink(c.pidpath);
    if (c.lockfd >= 0) {
        close(c.lockfd);
        unlink(c.lockpath);
    }
    if (c.once)
        return rc == 1 ? 0 : 1;
    return 0;
}
