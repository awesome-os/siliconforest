/* Original bounded runtime fixture support. No host/Darwin substitution. */
#ifndef CHONK_PROBE_COMMON_H
#define CHONK_PROBE_COMMON_H
#if !defined(__APPLE__) || !defined(__aarch64__)
#error this runtime fixture requires the pinned arm64 Darwin SDK
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define PROBE_CHILD "/usr/bin/darwin-spawn-whitelist-child"
#define PROBE_PREFIX "/private/tmp/chonk-spawn-whitelist-"
#define PROBE_SCAN_CAP 65536u
#define PROBE_CHANNEL_CAP 256u
#define PROBE_OPERATION_MS 8000u
#define PROBE_CHILD_MS 6000u
#define PROBE_REQUIRED_FLAGS (POSIX_SPAWN_CLOEXEC_DEFAULT|POSIX_SPAWN_SETSIGDEF|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETPGROUP)
_Static_assert(POSIX_SPAWN_CLOEXEC_DEFAULT==0x4000,"pinned SDK close policy");
_Static_assert(PROBE_REQUIRED_FLAGS==0x400e,"pinned SDK full flag set");

static inline int probe_errno(void) { return errno ? errno : EIO; }
static inline int probe_nonce(const char *s)
{
    size_t i;
    if (!s) return 0;
    for (i=0;i<48;i++) if (!((s[i]>='0' && s[i]<='9') ||
                              (s[i]>='a' && s[i]<='f'))) return 0;
    return s[48]==0;
}
static inline int probe_u32(const char *s, uint32_t *value)
{
    uint32_t n=0;
    const char *p=s;
    if (!s || !*s || (*s=='0' && s[1])) return EINVAL;
    while (*p) {
        uint32_t digit;
        if (*p<'0' || *p>'9') return EINVAL;
        digit=(uint32_t)(*p++-'0');
        if (n>(UINT32_MAX-digit)/10u) return ERANGE;
        n=n*10u+digit;
    }
    *value=n; return 0;
}
static inline int probe_now(uint64_t *milliseconds)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)<0) return probe_errno();
    if (t.tv_sec<0 || t.tv_nsec<0 || t.tv_nsec>=1000000000L) return EIO;
    *milliseconds=(uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
    return 0;
}
static inline int probe_before(uint64_t deadline)
{
    uint64_t now;
    int e=probe_now(&now);
    return e ? e : now>=deadline ? ETIMEDOUT : 0;
}
static inline int probe_ignore_pipe(void)
{
    struct sigaction action;
    memset(&action,0,sizeof(action)); action.sa_handler=SIG_IGN;
    if (sigemptyset(&action.sa_mask)<0 || sigaction(SIGPIPE,&action,NULL)<0)
        return probe_errno();
    return 0;
}
static inline int probe_flags(int fd, int cloexec)
{
    int flags=fcntl(fd,F_GETFD);
    if (flags<0) return probe_errno();
    if (cloexec) {
        if (fcntl(fd,F_SETFD,flags|FD_CLOEXEC)<0) return probe_errno();
        flags=fcntl(fd,F_GETFD);
        if (flags<0) return probe_errno();
        if (!(flags&FD_CLOEXEC)) return EIO;
    }
    flags=fcntl(fd,F_GETFL);
    if (flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) return probe_errno();
    flags=fcntl(fd,F_GETFL);
    if (flags<0) return probe_errno();
    if (!(flags&O_NONBLOCK)) return EIO;
    return 0;
}
static inline int probe_write(int fd,const void *data,size_t length,uint64_t deadline)
{
    const unsigned char *bytes=(const unsigned char *)data;
    size_t offset=0;
    while (offset<length) {
        ssize_t n;
        int e=probe_before(deadline);
        if (e) return e;
        n=write(fd,bytes+offset,length-offset);
        if (n>0) offset+=(size_t)n;
        else if (!n) return EIO;
        else if (errno==EINTR) continue;
        else if (errno==EAGAIN || errno==EWOULDBLOCK) {
            struct pollfd p={fd,POLLOUT,0};
            if (poll(&p,1,10)<0 && errno!=EINTR) return probe_errno();
        } else return probe_errno();
    }
    return 0;
}
#endif
