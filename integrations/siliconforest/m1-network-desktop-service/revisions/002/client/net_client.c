/* One ordinary native libc/BSD-socket client, invoked by a real foreground
 * shell. */
#ifndef __APPLE__
#error Native Darwin executable only; host tests exercise pure serializers.
#endif
#include "net_client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
_Static_assert(AF_INET == 2 && SOCK_STREAM == 1 && IPPROTO_TCP == 6,
               "selected Darwin constants");
_Static_assert(sizeof(struct sockaddr_in) == 16, "Darwin IPv4 sockaddr size");
_Static_assert(offsetof(struct sockaddr_in, sin_len) == 0 &&
                   offsetof(struct sockaddr_in, sin_family) == 1 &&
                   offsetof(struct sockaddr_in, sin_port) == 2 &&
                   offsetof(struct sockaddr_in, sin_addr) == 4 &&
                   offsetof(struct sockaddr_in, sin_zero) == 8,
               "Darwin IPv4 sockaddr offsets");
_Static_assert(sizeof(struct addrinfo) == 48 &&
                   offsetof(struct addrinfo, ai_addrlen) == 16 &&
                   offsetof(struct addrinfo, ai_canonname) == 24 &&
                   offsetof(struct addrinfo, ai_addr) == 32 &&
                   offsetof(struct addrinfo, ai_next) == 40,
               "selected SDK addrinfo LP64 ABI");
static uint64_t ms(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t) || t.tv_sec < 0 || t.tv_nsec < 0 ||
      t.tv_nsec >= 1000000000)
    return 0;
  uint64_t fraction = (uint64_t)t.tv_nsec / 1000000;
  if ((uint64_t)t.tv_sec > (UINT64_MAX - fraction) / 1000)
    return 0;
  return (uint64_t)t.tv_sec * 1000 + fraction;
}
static int write_all(int fd, const void *data, size_t n) {
  const uint8_t *p = data;
  for (unsigned a = 0; n && a < 4096; a++) {
    ssize_t v = write(fd, p, n);
    if (v < 0 && errno == EINTR)
      continue;
    if (v <= 0)
      return -1;
    p += v;
    n -= (size_t)v;
  }
  return n ? -1 : 0;
}
static int directory(const char *path) {
  int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return -1;
  const char *p = path + 1;
  char part[256];
  while (*p) {
    const char *end = strchr(p, '/');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if (!n || n >= sizeof(part)) {
      close(fd);
      return -1;
    }
    memcpy(part, p, n);
    part[n] = 0;
    int next = openat(
        fd, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    int rc = close(fd);
    if (next < 0 || rc) {
      if (next >= 0)
        close(next);
      return -1;
    }
    fd = next;
    int fd_flags = fcntl(fd, F_GETFD);
    struct stat st;
    if (fd_flags < 0 || fstat(fd, &st) || !S_ISDIR(st.st_mode) || st.st_uid ||
        st.st_mode & 0022) {
      close(fd);
      return -1;
    }
    if (!strcmp(part, "darwin-desktop-network") ||
        !strncmp(part, "session-", 8) || !end) {
      if ((st.st_mode & 07777) != 0700) {
        close(fd);
        return -1;
      }
    }
    if (!end)
      break;
    p = end + 1;
  }
  return fd;
}
static int new_file(int dir, const char *name) {
  int fd = openat(
      dir, name,
      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
  if (fd < 0)
    return -1;
  int fd_flags = fcntl(fd, F_GETFD);
  struct stat st;
  if (fd_flags < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid ||
      st.st_nlink != 1 || (st.st_mode & 07777) != 0600 ||
      !(fd_flags & FD_CLOEXEC)) {
    close(fd);
    return -1;
  }
  return fd;
}
static int pid_record(int dir, int32_t pid) {
  char line[32];
  int n = snprintf(line, sizeof(line), "%d\n", pid);
  int fd = new_file(dir, "network-client.pid");
  if (fd < 0)
    return -1;
  int result = n > 0 && (size_t)n < sizeof(line) &&
                       !write_all(fd, line, (size_t)n) && !fsync(fd)
                   ? 0
                   : -1;
  if (close(fd))
    result = -1;
  return result;
}
static int commit(int dir, const struct dn_result *r) {
  char data[DN_JSON_CAP];
  size_t n;
  if (dn_result_format(r, data, sizeof(data), &n))
    return -1;
  int fd = new_file(dir, "network-client.json.tmp");
  if (fd < 0)
    return -1;
  int result = write_all(fd, data, n) || fsync(fd) ? -1 : 0;
  if (close(fd))
    result = -1;
  if (!result &&
      linkat(dir, "network-client.json.tmp", dir, "network-client.json", 0))
    result = -1;
  if (unlinkat(dir, "network-client.json.tmp", 0))
    result = -1;
  return result;
}
static int flags(int fd) {
  int a = fcntl(fd, F_GETFL), b = fcntl(fd, F_GETFD);
  return a < 0 || b < 0 || fcntl(fd, F_SETFL, a | O_NONBLOCK) ||
                 fcntl(fd, F_SETFD, b | FD_CLOEXEC) ||
                 (a = fcntl(fd, F_GETFL)) < 0 || !(a & O_NONBLOCK) ||
                 (b = fcntl(fd, F_GETFD)) < 0 || !(b & FD_CLOEXEC)
             ? -1
             : 0;
}
static int ready(int fd, int writing, uint64_t end) {
  if (fd < 0 || fd >= FD_SETSIZE)
    return -1;
  for (;;) {
    uint64_t now = ms();
    if (!now || now >= end) {
      errno = ETIMEDOUT;
      return -1;
    }
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval pause = {0, 20000};
    int rc = select(fd + 1, writing ? NULL : &set, writing ? &set : NULL, NULL,
                    &pause);
    if (rc > 0)
      return 0;
    if (rc < 0 && errno != EINTR)
      return -1;
  }
}
static void fail(struct dn_result *r, const char *error, int code) {
  if (!strcmp(r->error, "none")) {
    r->error = error;
    r->error_errno = code > 0 ? code : 0;
  }
}
static int endpoint(int fd, int peer, uint8_t ip[4], unsigned *port) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  socklen_t n = sizeof(sa);
  int rc = peer ? getpeername(fd, (struct sockaddr *)&sa, &n)
                : getsockname(fd, (struct sockaddr *)&sa, &n);
  if (rc || n != sizeof(sa) || sa.sin_len != sizeof(sa) ||
      sa.sin_family != AF_INET)
    return -1;
  memcpy(ip, &sa.sin_addr, 4);
  *port = ntohs(sa.sin_port);
  return 0;
}
int main(int argc, char **argv) {
  if (argc != 7 || strcmp(argv[1], "--instance") ||
      strcmp(argv[3], "--records") || strcmp(argv[5], "--seconds"))
    return 2;
  struct dn_result r;
  memset(&r, 0, sizeof(r));
  r.error = "none";
  r.pid = getpid();
  r.ppid = getppid();
  r.sid = getsid(0);
  r.pgid = getpgrp();
  r.foreground_pgid = tcgetpgrp(0);
  r.started_ms = ms();
  if (dn_identity_parse(argv[4], argv[2], &r.identity) || strlen(argv[6]) > 2 ||
      argv[6][0] < '1' || argv[6][0] > '6' || argv[6][1] < '0' ||
      argv[6][1] > '9' || argv[6][2])
    return 2;
  unsigned seconds =
      (unsigned)(argv[6][0] - '0') * 10 + (unsigned)(argv[6][1] - '0');
  if (seconds < 10 || seconds > 60 || !r.started_ms ||
      r.started_ms > UINT64_MAX - 60000 || r.pid < 2 || r.ppid < 2 ||
      geteuid() || getuid())
    return 2;
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) || limit.rlim_cur < 3 ||
      limit.rlim_cur > 65536)
    fail(&r, "entry_fd", errno);
  else {
    r.entry_fd_bound = (uint32_t)limit.rlim_cur;
    int good = 1;
    for (int fd = 0; fd < 3; fd++) {
      int v = fcntl(fd, F_GETFD);
      if (v < 0 || (v & FD_CLOEXEC)) {
        good = 0;
        break;
      }
    }
    for (int fd = 3; fd < (int)r.entry_fd_bound; fd++) {
      errno = 0;
      int v = fcntl(fd, F_GETFD);
      if (v >= 0 || errno != EBADF) {
        good = 0;
        break;
      }
    }
    r.entry_fd_checked = good;
    if (!good)
      fail(&r, "entry_fd", errno);
  }
  if (r.sid != r.ppid || r.pgid != r.ppid || r.foreground_pgid != r.ppid ||
      !isatty(0) || !isatty(1) || !isatty(2))
    fail(&r, "tty", errno);
  int dir = directory(argv[4]);
  if (dir < 0)
    return 1;
  if (pid_record(dir, r.pid)) {
    close(dir);
    return 1;
  }
  char line[256];
  int line_n =
      snprintf(line, sizeof(line),
               "DL30_CLIENT_BEGIN session=%s instance=%s pid=%d shell_pid=%d\n",
               r.identity.nonce, r.identity.instance, r.pid, r.ppid);
  if (line_n <= 0 || (size_t)line_n >= sizeof(line) ||
      write_all(1, line, (size_t)line_n))
    fail(&r, "tty", errno);
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  if (sigemptyset(&action.sa_mask) || sigaction(SIGPIPE, &action, NULL))
    fail(&r, "signal", errno);
  uint64_t end = r.started_ms + (uint64_t)seconds * 1000;
  struct addrinfo *answer = NULL;
  if (!strcmp(r.error, "none")) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;
    errno = 0;
    r.gai_rc = getaddrinfo("example.com", "80", &hints, &answer);
    if (r.gai_rc == EAI_SYSTEM)
      r.system_errno = errno;
    if (r.gai_rc)
      fail(&r, "gai", 0);
    else {
      struct addrinfo *seen[32];
      unsigned count = 0;
      for (struct addrinfo *p = answer; p;) {
        if (count == 32) {
          fail(&r, "address", 0);
          break;
        }
        int repeat = 0;
        for (unsigned i = 0; i < count; i++)
          if (seen[i] == p)
            repeat = 1;
        if (repeat) {
          fail(&r, "address", 0);
          break;
        }
        seen[count++] = p;
        const struct sockaddr_in *sa = (const struct sockaddr_in *)p->ai_addr;
        if (p->ai_family != AF_INET || p->ai_socktype != SOCK_STREAM ||
            p->ai_protocol != IPPROTO_TCP || p->ai_addrlen != sizeof(*sa) ||
            !sa || sa->sin_len != sizeof(*sa) || sa->sin_family != AF_INET ||
            ntohs(sa->sin_port) != 80 ||
            !eg_ipv4_global((const uint8_t *)&sa->sin_addr)) {
          fail(&r, "address", 0);
          break;
        }
        int reserved = 0;
        for (size_t i = 0; i < sizeof(sa->sin_zero); i++)
          reserved |= sa->sin_zero[i];
        if (reserved) {
          fail(&r, "address", 0);
          break;
        }
        memcpy(r.addresses[r.address_count++], &sa->sin_addr, 4);
        p = p->ai_next;
      }
      if (!r.address_count)
        fail(&r, "address", 0);
      if (!strcmp(r.error, "none")) {
        r.address_list_complete = 1;
        freeaddrinfo(answer);
        answer = NULL;
      }
    }
  }
  int fd = -1;
  if (!strcmp(r.error, "none") && ms() >= end)
    fail(&r, "deadline", ETIMEDOUT);
  if (!strcmp(r.error, "none")) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      fail(&r, "socket", errno);
    else {
      r.socket_created = 1;
      if (flags(fd))
        fail(&r, "socket", errno);
    }
  }
  if (!strcmp(r.error, "none")) {
    int mss = 256;
    socklen_t n = sizeof(mss);
    if (setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &mss, sizeof(mss)) ||
        getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &mss, &n) || n != sizeof(mss) ||
        mss != 256)
      fail(&r, "socket", errno);
    else
      r.tcp_maxseg_observed = mss;
  }
  if (!strcmp(r.error, "none")) {
    struct sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
    peer.sin_len = sizeof(peer);
    peer.sin_family = AF_INET;
    peer.sin_port = htons(80);
    memcpy(&peer.sin_addr, r.addresses[0], 4);
    int rc = connect(fd, (const struct sockaddr *)&peer, sizeof(peer));
    if (rc < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK &&
        errno != EINTR)
      fail(&r, "connect", errno);
    if (!strcmp(r.error, "none") && rc < 0) {
      if (ready(fd, 1, end))
        fail(&r, "connect", errno);
      else {
        int error = 0;
        socklen_t n = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &n) ||
            n != sizeof(error) || error)
          fail(&r, "connect", error ? error : errno);
      }
    }
    if (!strcmp(r.error, "none") &&
        (endpoint(fd, 0, r.local, &r.local_port) ||
         endpoint(fd, 1, r.peer, &r.peer_port) ||
         memcmp(r.local, "\x0a\0\x02\x0f", 4) || r.local_port < 1024 ||
         r.peer_port != 80 || memcmp(r.peer, r.addresses[0], 4)))
      fail(&r, "tuple", errno);
  }
  if (!strcmp(r.error, "none")) {
    size_t n = 0;
    if (eg_http_request(r.identity.nonce, 48, r.request, sizeof(r.request),
                        &n) != EG_OK ||
        n != 186)
      fail(&r, "http", 0);
    for (unsigned a = 0;
         !strcmp(r.error, "none") && r.request_bytes < n && a < 4096; a++) {
      if (ready(fd, 1, end)) {
        fail(&r, "send", errno);
        break;
      }
      ssize_t wrote =
          send(fd, r.request + r.request_bytes, n - r.request_bytes, 0);
      if (wrote < 0 &&
          (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        continue;
      if (wrote <= 0) {
        fail(&r, "send", errno);
        break;
      }
      r.request_bytes += (size_t)wrote;
    }
    if (r.request_bytes != 186)
      fail(&r, "send", 0);
  }
  for (unsigned a = 0; !strcmp(r.error, "none") && !r.peer_eof && a < 4096;
       a++) {
    if (ready(fd, 0, end)) {
      fail(&r, "recv", errno);
      break;
    }
    uint8_t extra;
    void *dest = r.response_bytes < sizeof(r.response)
                     ? r.response + r.response_bytes
                     : &extra;
    size_t available = r.response_bytes < sizeof(r.response)
                           ? sizeof(r.response) - r.response_bytes
                           : 1;
    ssize_t n = recv(fd, dest, available, 0);
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
      continue;
    if (n < 0) {
      fail(&r, "recv", errno);
      break;
    }
    if (!n) {
      r.peer_eof = 1;
      break;
    }
    if (dest == &extra) {
      fail(&r, "http", 0);
      break;
    }
    r.response_bytes += (size_t)n;
  }
  if (!strcmp(r.error, "none") &&
      (!r.peer_eof ||
       eg_http_response(r.response, r.response_bytes, 1, &r.http) != EG_OK))
    fail(&r, "http", 0);
  if (!strcmp(r.error, "none")) {
    if (shutdown(fd, SHUT_WR))
      fail(&r, "shutdown", errno);
    else
      r.request_shutdown = 1;
  }
  if (fd >= 0) {
    if (close(fd))
      fail(&r, "close", errno);
    else
      r.socket_closed = 1;
    fd = -1;
  }
  uint64_t now = ms();
  if (!now || now < r.started_ms || now >= end)
    fail(&r, "deadline", ETIMEDOUT);
  r.elapsed_ms = now >= r.started_ms ? now - r.started_ms : 0;
  int rc = strcmp(r.error, "none") ? 1 : 0;
  if (commit(dir, &r))
    rc = 1;
  if (close(dir))
    rc = 1;
  uint64_t committed = ms();
  if (!committed || committed < now || committed >= end)
    rc = 1;
  line_n =
      snprintf(line, sizeof(line),
               "DL30_CLIENT_DONE session=%s instance=%s pid=%d status=%d\n",
               r.identity.nonce, r.identity.instance, r.pid, rc);
  if (line_n <= 0 || (size_t)line_n >= sizeof(line) ||
      write_all(1, line, (size_t)line_n))
    rc = 1;
  uint64_t returned = ms();
  if (!returned || returned < committed || returned >= end)
    rc = 1;
  return rc;
}
