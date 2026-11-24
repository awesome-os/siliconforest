/* Original finite utun-to-whitespace-framed link. No physical device writes.
 * Host ingress and every possible suffix are shell whitespace if this daemon
 * dies. Initial test reaches only the controlled slirp host alias TCP/UDP
 * echoes. */
#ifndef __APPLE__
#error Darwin guest only
#endif
#include "transport.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/sys_domain.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define QMAX 16
#define LINE 3300
#define READ_BUDGET 4
struct item {
  size_t length, offset;
  uint8_t data[LINE];
};
struct queue {
  struct item item[QMAX];
  unsigned head, count;
};
struct state {
  struct dln_tx tx;
  struct dln_rx rx;
  struct queue output, input;
  char nonce[49], name[IFNAMSIZ];
  size_t line_used;
  uint8_t bit_value;
  unsigned bit_count;
  uint64_t console_bytes_in;
  int utun, ctl, tcp, udp, connected, udp_sent, tcp_sent, udp_ok, tcp_ok;
  int peer_open, peer_close, close_sent, tty_saved, flags_saved;
  int old_in, old_out;
  struct termios terminal;
  uint8_t tcp_body[80];
  size_t tcp_used, tcp_written;
  char tcp_want[80], udp_want[80];
  size_t tcp_n, udp_n;
  uint64_t deadline, closing, close_deadline, frames_out, frames_in,
      packets_out, packets_in, ignored_family;
  const char *error;
  unsigned errno_saved;
};
static volatile sig_atomic_t stopped;
static void stop_now(int n) {
  (void)n;
  stopped = 1;
}
static uint64_t ms(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    return 0;
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static int nonblock(int fd) {
  int f = fcntl(fd, F_GETFL);
  return f < 0 ? -1 : fcntl(fd, F_SETFL, f | O_NONBLOCK);
}
static int fail(struct state *s, const char *e) {
  if (!s->error) {
    s->error = e;
    s->errno_saved = (unsigned)errno;
  }
  return -1;
}
static int push(struct queue *q, const uint8_t *p, size_t n) {
  if (!p || !n || n > LINE || q->count == QMAX)
    return -1;
  struct item *a = &q->item[(q->head + q->count) % QMAX];
  memcpy(a->data, p, n);
  a->length = n;
  a->offset = 0;
  q->count++;
  return 0;
}
static int flush(struct state *s, struct queue *q, int fd, int packet) {
  if (!q->count)
    return 0;
  struct item *a = &q->item[q->head];
  ssize_t n = write(fd, a->data + a->offset, a->length - a->offset);
  if (n < 0 && (errno == EAGAIN || errno == EINTR))
    return 0;
  if (n <= 0 || (packet && (size_t)n != a->length))
    return fail(s, packet ? "utun_write" : "console_write");
  a->offset += (size_t)n;
  if (a->offset == a->length) {
    q->head = (q->head + 1) % QMAX;
    q->count--;
  }
  return 0;
}
static int emit(struct state *s, uint8_t type, const uint8_t *p, size_t n) {
  uint8_t frame[DLNET_FRAME_BYTES];
  char line[LINE];
  size_t bytes = 0;
  if (s->frames_out >= 128)
    return fail(s, "transmit_frame_cap");
  if (s->output.count == QMAX)
    return fail(s, "console_queue_full");
  enum dln_error e =
      dln_encode(&s->tx, type, p, n, frame, sizeof(frame), &bytes);
  if (e)
    return fail(s, dln_error_name(e));
  int head = snprintf(line, sizeof(line), "DL26_TX_%s ", s->nonce);
  static const char hex[] = "0123456789abcdef";
  if (head <= 0 || (size_t)head + 2 * bytes + 1 > sizeof(line))
    return fail(s, "encoded_length");
  for (size_t i = 0; i < bytes; i++) {
    line[head + 2 * i] = hex[frame[i] >> 4];
    line[head + 2 * i + 1] = hex[frame[i] & 15];
  }
  line[head + 2 * bytes] = '\n';
  if (push(&s->output, (uint8_t *)line, (size_t)head + 2 * bytes + 1))
    return fail(s, "console_queue_full");
  s->frames_out++;
  if (type == DLNET_IPV4)
    s->packets_out++;
  return 0;
}
static int acknowledge(struct state *s) {
  char line[160];
  int n=snprintf(line,sizeof(line),"DL26_LINK_ACK_%s bytes=%llu\n",s->nonce,
                 (unsigned long long)s->console_bytes_in);
  if(n<=0 || (size_t)n>=sizeof(line) ||
     push(&s->output,(const uint8_t *)line,(size_t)n))
    return fail(s,"acknowledgement_queue");
  return 0;
}
static int received(void *ctx, uint8_t type, uint64_t sequence,
                    const uint8_t *p, size_t n) {
  struct state *s = ctx;
  (void)sequence;
  if (s->frames_in >= 128)
    return fail(s, "receive_frame_cap");
  s->frames_in++;
  if (type == DLNET_OPEN) {
    if (s->peer_open)
      return fail(s, "duplicate_open");
    s->peer_open = 1;
    return 0;
  }
  if (type == DLNET_CLOSE) {
    s->peer_close = 1;
    return s->closing ? 0 : fail(s, "peer_closed_early");
  }
  if (type != DLNET_IPV4 || !s->peer_open || s->peer_close)
    return fail(s, "frame_phase");
  uint8_t packet[DLNET_MTU + 4];
  uint32_t family = htonl(AF_INET);
  memcpy(packet, &family, 4);
  memcpy(packet + 4, p, n);
  if (push(&s->input, packet, n + 4))
    return fail(s, "utun_queue_full");
  s->packets_in++;
  return 0;
}
static int line_received(struct state *s) {
  if (s->line_used != 1 && s->line_used != 2)
    return fail(s, "ingress_bit_length");
  s->bit_value = (uint8_t)((s->bit_value << 1) | (s->line_used - 1));
  if (++s->bit_count == 8) {
    uint8_t value = s->bit_value;
    s->bit_count = 0;
    s->bit_value = 0;
    enum dln_error e = dln_feed(&s->rx, &value, 1, received, s);
    if (e)
      return fail(s, dln_error_name(e));
  }
  return 0;
}
static int read_console(struct state *s) {
  uint8_t data[4096];
  for (unsigned k = 0; k < READ_BUDGET; k++) {
    ssize_t n = read(STDIN_FILENO, data, sizeof(data));
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
      return 0;
    if (n <= 0)
      return fail(s, "console_eof");
    s->console_bytes_in += (uint64_t)n;
    if (s->console_bytes_in > 131072)
      return fail(s, "ingress_byte_cap");
    for (ssize_t i = 0; i < n; i++) {
      if (data[i] == '\n') {
        if (line_received(s))
          return -1;
        s->line_used = 0;
      } else {
        if (data[i] != ' ' || s->line_used == 2)
          return fail(s, "ingress_alphabet");
        s->line_used++;
      }
    }
  }
  return 0;
}
static struct sockaddr_in addr(uint32_t ip, uint16_t port) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_len = sizeof(a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(ip);
  a.sin_port = htons(port);
  return a;
}
static int up(struct state *s) {
  struct ifaliasreq a;
  memset(&a, 0, sizeof(a));
  strlcpy(a.ifra_name, s->name, sizeof(a.ifra_name));
  struct sockaddr_in local = addr(0x0a00020f, 0), peer = addr(0x0a000202, 0),
                     mask = addr(0xffffffff, 0);
  memcpy(&a.ifra_addr, &local, sizeof(local));
  memcpy(&a.ifra_broadaddr, &peer, sizeof(peer));
  memcpy(&a.ifra_mask, &mask, sizeof(mask));
  if (ioctl(s->ctl, SIOCAIFADDR, &a) < 0)
    return fail(s, "utun_address");
  struct ifreq f;
  memset(&f, 0, sizeof(f));
  strlcpy(f.ifr_name, s->name, sizeof(f.ifr_name));
  f.ifr_mtu = DLNET_MTU;
  if (ioctl(s->ctl, SIOCSIFMTU, &f) < 0)
    return fail(s, "utun_mtu");
  if (ioctl(s->ctl, SIOCGIFFLAGS, &f) < 0)
    return fail(s, "utun_flags");
  f.ifr_flags |= IFF_UP;
  return ioctl(s->ctl, SIOCSIFFLAGS, &f) < 0 ? fail(s, "utun_up") : 0;
}
static int setup_utun(struct state *s) {
  struct ctl_info info;
  struct sockaddr_ctl control;
  memset(&info, 0, sizeof(info));
  memset(&control, 0, sizeof(control));
  s->utun = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
  s->ctl = socket(AF_INET, SOCK_DGRAM, 0);
  if (s->utun < 0 || s->ctl < 0)
    return fail(s, "utun_socket");
  strlcpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name));
  if (ioctl(s->utun, CTLIOCGINFO, &info) < 0)
    return fail(s, "utun_control");
  control.sc_len = sizeof(control);
  control.sc_family = AF_SYSTEM;
  control.ss_sysaddr = AF_SYS_CONTROL;
  control.sc_id = info.ctl_id;
  if (connect(s->utun, (struct sockaddr *)&control, sizeof(control)) < 0)
    return fail(s, "utun_connect");
  socklen_t size = sizeof(s->name);
  if (getsockopt(s->utun, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, s->name, &size) <
          0 ||
      !size || size > sizeof(s->name) || !memchr(s->name, 0, size))
    return fail(s, "utun_name");
  if (nonblock(s->utun))
    return fail(s, "utun_nonblock");
  if (up(s))
    return -1;
  return 0;
}
static int tty_raw(struct state *s) {
  if (tcgetattr(0, &s->terminal) < 0)
    return fail(s, "terminal_get");
  s->tty_saved = 1;
  struct termios t = s->terminal;
  cfmakeraw(&t);
  if (tcsetattr(0, TCSANOW, &t) < 0)
    return fail(s, "terminal_raw");
  s->old_in = fcntl(0, F_GETFL);
  s->old_out = fcntl(1, F_GETFL);
  if (s->old_in < 0 || s->old_out < 0)
    return fail(s, "terminal_flags");
  s->flags_saved = 1;
  if (fcntl(0, F_SETFL, s->old_in | O_NONBLOCK) < 0 ||
      fcntl(1, F_SETFL, s->old_out | O_NONBLOCK) < 0)
    return fail(s, "terminal_nonblock");
  return 0;
}
static int start_clients(struct state *s, unsigned tcp_port,
                         unsigned udp_port) {
  s->tcp = socket(AF_INET, SOCK_STREAM, 0);
  s->udp = socket(AF_INET, SOCK_DGRAM, 0);
  if (s->tcp < 0 || s->udp < 0 || nonblock(s->tcp) || nonblock(s->udp))
    return fail(s, "client_sockets");
  struct sockaddr_in target = addr(0x0a000202, (uint16_t)tcp_port);
  if (connect(s->tcp, (struct sockaddr *)&target, sizeof(target)) == 0)
    s->connected = 1;
  else if (errno != EINPROGRESS)
    return fail(s, "tcp_connect");
  target = addr(0x0a000202, (uint16_t)udp_port);
  ssize_t n = sendto(s->udp, s->udp_want, s->udp_n, 0,
                     (struct sockaddr *)&target, sizeof(target));
  if (n != (ssize_t)s->udp_n)
    return fail(s, "udp_send");
  s->udp_sent = 1;
  return 0;
}
static int clients(struct state *s, short tcp_events, short udp_events,
                   unsigned udp_port) {
  if (!s->connected && (tcp_events & (POLLOUT | POLLERR | POLLHUP))) {
    int e = 0;
    socklen_t n = sizeof(e);
    if (getsockopt(s->tcp, SOL_SOCKET, SO_ERROR, &e, &n) < 0)
      return fail(s, "tcp_complete_query");
    if (e) {
      errno = e;
      return fail(s, "tcp_complete");
    }
    s->connected = 1;
  }
  if (s->connected && !s->tcp_sent && (tcp_events & POLLOUT)) {
    ssize_t n = send(s->tcp, s->tcp_want + s->tcp_written,
                     s->tcp_n - s->tcp_written, 0);
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
      return 0;
    if (n <= 0 || (size_t)n > s->tcp_n - s->tcp_written)
      return fail(s, "tcp_send");
    s->tcp_written += (size_t)n;
    s->tcp_sent = s->tcp_written == s->tcp_n;
  }
  if ((tcp_events & (POLLIN | POLLHUP)) && !s->tcp_ok) {
    ssize_t n = recv(s->tcp, s->tcp_body + s->tcp_used,
                     sizeof(s->tcp_body) - s->tcp_used, 0);
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
      return 0;
    if (n <= 0)
      return fail(s, "tcp_receive");
    s->tcp_used += (size_t)n;
    if (s->tcp_used > s->tcp_n || memcmp(s->tcp_body, s->tcp_want, s->tcp_used))
      return fail(s, "tcp_echo_mismatch");
    if (s->tcp_used == s->tcp_n)
      s->tcp_ok = 1;
  }
  if ((udp_events & POLLIN) && !s->udp_ok) {
    uint8_t packet[80];
    struct sockaddr_in from;
    socklen_t n = sizeof(from);
    ssize_t bytes = recvfrom(s->udp, packet, sizeof(packet), 0,
                             (struct sockaddr *)&from, &n);
    if (bytes < 0 && (errno == EAGAIN || errno == EINTR))
      return 0;
    if (bytes != (ssize_t)s->udp_n || memcmp(packet, s->udp_want, s->udp_n) ||
        n != sizeof(from) || from.sin_family != AF_INET ||
        ntohl(from.sin_addr.s_addr) != 0x0a000202 ||
        ntohs(from.sin_port) != udp_port)
      return fail(s, "udp_echo_mismatch");
    s->udp_ok = 1;
  }
  return 0;
}
static unsigned number(const char *p, unsigned high) {
  unsigned n = 0;
  if (!p || !*p)
    return 0;
  for (; *p; p++) {
    if (*p < '0' || *p > '9' || n > (high - (unsigned)(*p - '0')) / 10)
      return 0;
    n = n * 10 + (unsigned)(*p - '0');
  }
  return n <= high ? n : 0;
}
static int close_fd(struct state *s, int *fd) {
  if (*fd < 0)
    return 0;
  int n = *fd;
  *fd = -1;
  return close(n) < 0 ? fail(s, "close_fd") : 0;
}
/* ARM console supports d_select, but does not opt into vnode kqueue filters.
 * XNU poll uses kqueue and rejects this tty. select follows cnselect/ttselect. */
static int wait_ready(struct pollfd f[5]) {
  fd_set read_set, write_set, except_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&except_set);
  int high = -1;
  for (unsigned i = 0; i < 5; i++) {
    f[i].revents = 0;
    int fd = f[i].fd;
    if (fd < 0 || !f[i].events)
      continue;
    if (fd >= FD_SETSIZE) {
      errno = EINVAL;
      return -1;
    }
    if (f[i].events & POLLIN)
      FD_SET(fd, &read_set);
    if (f[i].events & POLLOUT)
      FD_SET(fd, &write_set);
    FD_SET(fd, &except_set);
    if (fd > high)
      high = fd;
  }
  struct timeval delay = {0, 20000};
  int rc = select(high + 1, &read_set, &write_set, &except_set, &delay);
  if (rc <= 0)
    return rc;
  for (unsigned i = 0; i < 5; i++) {
    int fd = f[i].fd;
    if (fd < 0 || !f[i].events)
      continue;
    if (FD_ISSET(fd, &read_set))
      f[i].revents |= POLLIN;
    if (FD_ISSET(fd, &write_set))
      f[i].revents |= POLLOUT;
    if (FD_ISSET(fd, &except_set))
      f[i].revents |= POLLERR;
  }
  return rc;
}
int main(int argc, char **argv) {
  if (argc != 9 || strcmp(argv[1], "--nonce") || strcmp(argv[3], "--seconds") ||
      strcmp(argv[5], "--tcp-port") || strcmp(argv[7], "--udp-port"))
    return 2;
  uint8_t nonce[DLNET_NONCE_BYTES];
  unsigned seconds = number(argv[4], 60), tp = number(argv[6], 65535),
           up_port = number(argv[8], 65535);
  if (!dln_nonce_hex(argv[2], strlen(argv[2]), nonce) || seconds < 10 || !tp ||
      !up_port || geteuid() != 0)
    return 2;
  struct state *s = calloc(1, sizeof(*s));
  if (!s)
    return 1;
  s->utun = s->ctl = s->tcp = s->udp = -1;
  memcpy(s->nonce, argv[2], 49);
  s->tcp_n = (size_t)snprintf(s->tcp_want, sizeof(s->tcp_want), "DL26-TCP %s",
                              s->nonce);
  s->udp_n = (size_t)snprintf(s->udp_want, sizeof(s->udp_want), "DL26-UDP %s",
                              s->nonce);
  struct sigaction a;
  memset(&a, 0, sizeof(a));
  a.sa_handler = stop_now;
  sigemptyset(&a.sa_mask);
  if (sigaction(SIGINT, &a, NULL) < 0 || sigaction(SIGTERM, &a, NULL) < 0 ||
      sigaction(SIGHUP, &a, NULL) < 0) {
    fail(s, "signal_setup");
    goto cleanup;
  }
  a.sa_handler = SIG_IGN;
  if (sigaction(SIGPIPE, &a, NULL) < 0 || sigaction(SIGTTOU, &a, NULL) < 0) {
    fail(s, "signal_setup");
    goto cleanup;
  }
  s->deadline = ms() + seconds * 1000;
  if (dln_tx_init(&s->tx, nonce, DLNET_GUEST_TO_HOST) ||
      dln_rx_init(&s->rx, nonce, DLNET_HOST_TO_GUEST)) {
    fail(s, "codec_init");
    goto cleanup;
  }
  if (setup_utun(s) || tty_raw(s))
    goto cleanup;
  char ready[180];
  int rn = snprintf(ready, sizeof(ready),
                    "\nDL26_LINK_READY_%s interface=%s mtu=1500\n", s->nonce,
                    s->name);
  if (push(&s->output, (uint8_t *)ready, (size_t)rn) ||
      emit(s, DLNET_OPEN, NULL, 0))
    goto cleanup;
  while (!s->error) {
    uint64_t now = ms();
    if (stopped) {
      fail(s, "signal");
      break;
    }
    if (!now || now >= s->deadline) {
      fail(s, "deadline");
      break;
    }
    if (s->peer_open && s->tcp < 0 && !s->closing &&
        start_clients(s, tp, up_port))
      break;
    if (s->tcp_ok && s->udp_ok && !s->closing) {
      s->closing = now;
      close_fd(s, &s->tcp);
      close_fd(s, &s->udp);
    }
    if (s->closing && now - s->closing >= 500 && !s->close_sent) {
      if (emit(s, DLNET_CLOSE, NULL, 0))
        break;
      s->close_sent = 1;
      s->close_deadline = now + 3000;
    }
    if (s->close_sent && s->peer_close && !s->output.count && !s->input.count) {
      if (s->line_used || s->bit_count || dln_end(&s->rx))
        fail(s, "unclean_stream_end");
      break;
    }
    if (s->close_sent && now >= s->close_deadline) {
      fail(s, "close_deadline");
      break;
    }
    struct pollfd f[5] = {
        {0, POLLIN, 0},
        {1, s->output.count ? POLLOUT : 0, 0},
        {s->utun,
         (short)((s->peer_open && !s->close_sent && s->output.count < QMAX - 2
                      ? POLLIN
                      : 0) |
                 (s->input.count ? POLLOUT : 0)),
         0},
        {s->tcp, (short)(POLLIN | (!s->tcp_sent ? POLLOUT : 0)), 0},
        {s->udp, POLLIN, 0}};
    int rc = wait_ready(f);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      fail(s, "select");
      break;
    }
    if (f[0].revents & (POLLIN | POLLHUP)) {
      uint64_t before=s->console_bytes_in;
      if(read_console(s))
        break;
      /* This credit is emitted only after every read byte was actually parsed.
       * The host will not deliver its next <=192-byte chunk until this ACK. */
      if(s->console_bytes_in!=before && acknowledge(s))
        break;
    }
    if ((f[1].revents & POLLOUT) && flush(s, &s->output, 1, 0))
      break;
    if ((f[2].revents & POLLOUT) && flush(s, &s->input, s->utun, 1))
      break;
    if (f[2].revents & POLLIN) {
      uint8_t packet[DLNET_MTU + 5];
      ssize_t n = read(s->utun, packet, sizeof(packet));
      if (n < 0 && (errno == EAGAIN || errno == EINTR))
        continue;
      if (n < 4 || n > DLNET_MTU + 4) {
        fail(s, "utun_packet_length");
        break;
      }
      uint32_t family;
      memcpy(&family, packet, 4);
      if (ntohl(family) != AF_INET) {
        if (++s->ignored_family > 128) {
          fail(s, "utun_family_cap");
          break;
        }
        continue;
      }
      if (emit(s, DLNET_IPV4, packet + 4, (size_t)n - 4))
        break;
    }
    if (s->tcp >= 0 && clients(s, f[3].revents, f[4].revents, up_port))
      break;
  }
cleanup:
  close_fd(s, &s->tcp);
  close_fd(s, &s->udp);
  close_fd(s, &s->utun);
  close_fd(s, &s->ctl);
  if (s->tty_saved && tcsetattr(0, TCSANOW, &s->terminal) < 0)
    fail(s, "terminal_restore");
  if (s->flags_saved) {
    int a = fcntl(0, F_SETFL, s->old_in);
    int b = fcntl(1, F_SETFL, s->old_out);
    if (a < 0 || b < 0)
      fail(s, "flags_restore");
  }
  int ok =
      !s->error && s->tcp_ok && s->udp_ok && s->peer_close && s->close_sent;
  printf(
      "\nDL26_LINK_RESULT_%s status=%s tcp_echo=%d udp_echo=%d frames_out=%llu "
      "frames_in=%llu packets_out=%llu packets_in=%llu error=%s errno=%u "
      "console_bytes_in=%llu partial_line=%zu partial_bits=%u\n",
      s->nonce, ok ? "PASS" : "FAILED", s->tcp_ok, s->udp_ok,
      (unsigned long long)s->frames_out, (unsigned long long)s->frames_in,
      (unsigned long long)s->packets_out, (unsigned long long)s->packets_in,
      s->error ? s->error : "none", s->errno_saved,
      (unsigned long long)s->console_bytes_in,s->line_used,s->bit_count);
  if (fflush(stdout) || ferror(stdout))
    ok = 0;
  free(s);
  return ok ? 0 : 1;
}
