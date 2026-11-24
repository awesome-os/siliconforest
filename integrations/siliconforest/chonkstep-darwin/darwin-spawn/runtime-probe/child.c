/* Actual exec'd child: never repairs the inherited descriptor boundary. */
#include "probe_common.h"

enum child_failure {
    C_LIMIT=1u, C_SCAN=2u, C_LOW=4u, C_TYPE=8u, C_FLAGS=16u,
    C_INPUT=32u, C_OUT=64u, C_ERR=128u, C_CLOCK=256u, C_STATUS=512u
};
static void failure(uint32_t *bits,int *first,uint32_t bit,int error)
{
    *bits|=bit; if (!*first) *first=error ? error : EIO;
}
int main(int argc,char **argv)
{
    struct rlimit limit;
    uint32_t bound=0,low=0,extra=0,bits=0;
    uint64_t now=0,deadline=0;
    int first=0,error,fd,io_setup_error=0;
    char input[PROBE_CHANNEL_CAP+1],expected[96],output[96],errline[96],report[PROBE_CHANNEL_CAP];
    size_t received=0;
    int eof=0;
    if (argc!=5 || strcmp(argv[1],"--nonce") || !probe_nonce(argv[2]) ||
        strcmp(argv[3],"--scan-limit") || probe_u32(argv[4],&bound) ||
        bound<=3 || bound>PROBE_SCAN_CAP) return 64;
    /* No allocation/open/close before inventory. A leaked FD stays visible. */
    error=probe_now(&now); if (error) return 65;
    deadline=now+PROBE_CHILD_MS;
    if (getrlimit(RLIMIT_NOFILE,&limit)<0) failure(&bits,&first,C_LIMIT,probe_errno());
    else if (limit.rlim_cur==RLIM_INFINITY || (uint64_t)limit.rlim_cur!=bound)
        failure(&bits,&first,C_LIMIT,EINVAL);
    for (fd=0;fd<(int)bound;fd++) {
        int flags=fcntl(fd,F_GETFD);
        if (flags>=0) {
            if (fd<4) {
                low|=1u<<(unsigned)fd;
                if (flags&FD_CLOEXEC) failure(&bits,&first,C_FLAGS,EINVAL);
            } else extra++;
        } else if (errno!=EBADF) failure(&bits,&first,C_SCAN,probe_errno());
        if (!(fd%64) && (error=probe_before(deadline))) {
            failure(&bits,&first,C_CLOCK,error); break;
        }
    }
    if (low!=15u || extra) failure(&bits,&first,C_LOW,EINVAL);
    for (fd=0;fd<4;fd++) {
        struct stat metadata;
        if (fstat(fd,&metadata)<0) failure(&bits,&first,C_TYPE,probe_errno());
        else if ((fd<3 && !S_ISFIFO(metadata.st_mode)) ||
                 (fd==3 && !S_ISSOCK(metadata.st_mode)))
            failure(&bits,&first,C_TYPE,EINVAL);
    }
    {
        struct sockaddr_un peer;
        socklen_t length=sizeof(peer),kind_length;
        int kind=0;
        memset(&peer,0,sizeof(peer)); kind_length=sizeof(kind);
        if (getpeername(3,(struct sockaddr *)&peer,&length)<0 ||
            getsockopt(3,SOL_SOCKET,SO_TYPE,&kind,&kind_length)<0)
            failure(&bits,&first,C_TYPE,probe_errno());
        else if (peer.sun_family!=AF_UNIX || kind!=SOCK_STREAM || kind_length!=sizeof(kind))
            failure(&bits,&first,C_TYPE,EINVAL);
    }
    /* The real spawn adapter resets SIGPIPE to default; install/check our own. */
    error=probe_ignore_pipe(); if (error) return 66;
    for (fd=0;fd<4;fd++) if ((error=probe_flags(fd,0))) {
        failure(&bits,&first,C_FLAGS,error);io_setup_error=1;
    }
    /* Never attempt potentially blocking I/O after nonblocking setup fails. */
    if (io_setup_error) return 68;
    while (!eof && received<sizeof(input)) {
        ssize_t n;
        error=probe_before(deadline);
        if (error) {failure(&bits,&first,C_INPUT,error);break;}
        n=read(0,input+received,sizeof(input)-received);
        if (n>0) received+=(size_t)n;
        else if (!n) eof=1;
        else if (errno==EINTR) continue;
        else if (errno==EAGAIN || errno==EWOULDBLOCK) {
            struct pollfd p={0,POLLIN,0};
            if (poll(&p,1,10)<0 && errno!=EINTR) {failure(&bits,&first,C_INPUT,probe_errno());break;}
        } else {failure(&bits,&first,C_INPUT,probe_errno());break;}
    }
    (void)snprintf(expected,sizeof(expected),"CHONKFD/1 IN %s\n",argv[2]);
    if (!eof || received!=strlen(expected) || memcmp(input,expected,received))
        failure(&bits,&first,C_INPUT,EPROTO);
    (void)snprintf(output,sizeof(output),"CHONKFD/1 OUT %s\n",argv[2]);
    (void)snprintf(errline,sizeof(errline),"CHONKFD/1 ERR %s\n",argv[2]);
    error=probe_write(1,output,strlen(output),deadline); if (error) failure(&bits,&first,C_OUT,error);
    error=probe_write(2,errline,strlen(errline),deadline); if (error) failure(&bits,&first,C_ERR,error);
    error=snprintf(report,sizeof(report),"CHONKFD/1 RESULT %s %ld %u %u %u %u %d\n",
                   argv[2],(long)getpid(),bound,low,extra,bits,first);
    if (error<0 || (size_t)error>=sizeof(report)) return 67;
    error=probe_write(3,report,(size_t)error,deadline);
    if (error) bits|=C_STATUS;
    return bits ? 1 : 0;
}
