/* Real managed-spawn runtime prerequisite. No fallback spawn implementation. */
#include "probe_common.h"
#include "../darwin_spawn.h"
#include <sys/utsname.h>
#include <sys/wait.h>

enum role {
    INPUT_CHILD, INPUT_PARENT, OUTPUT_PARENT, OUTPUT_CHILD,
    ERROR_PARENT, ERROR_CHILD, STATUS_PARENT, STATUS_CHILD,
    CONTROL_SEND, CONTROL_RECEIVE, AMBIENT_OPEN, AMBIENT_RECEIVED,
    LOG_OUTPUT, LOG_ERROR, LOG_STATUS, RESULT, ROLE_COUNT
};
struct channel { unsigned char bytes[PROBE_CHANNEL_CAP]; size_t count; int eof,overflow; };
struct error_record { const char *stage; int error; };
struct owned_child { pid_t pid; int live,reaped,lost,status,term_sent,kill_sent,wait_error; };
struct child_report { uint32_t pid,bound,low,extra,bits,first_errno; int valid; };
struct probe {
    const char *nonce;
    char directory[160];
    int fds[ROLE_COUNT],spawn_called,spawn_return,ambient_open_clear,ambient_received_clear;
    uint32_t bound;
    struct rlimit limit;
    int limit_checked,uname_checked,ambient_held,unknown_rights_cleanup;
    struct utsname name;
    struct chonk_spawn_result_v1 spawn;
    struct owned_child child;
    struct channel channels[3];
    struct child_report report;
    size_t input_sent;
    int exact_out,exact_err,all_eof;
    struct error_record errors[64];
    size_t error_count;
    int error_overflow;
};
static void fail(struct probe *p,const char *stage,int error)
{
    if (p->error_count<sizeof(p->errors)/sizeof(p->errors[0])) {
        p->errors[p->error_count].stage=stage;
        p->errors[p->error_count++].error=error ? error : EIO;
    } else p->error_overflow=1;
}
static void retire(struct probe *p,enum role role)
{
    int fd=p->fds[role];
    p->fds[role]=-1;
    /* Close is never retried, including EINTR; ownership is not reused. */
    if (fd>=0 && close(fd)<0) fail(p,"owned_close",probe_errno());
}
static int pair(struct probe *p,enum role first,int socket_kind)
{
    int descriptors[2],error;
    if ((socket_kind ? socketpair(AF_UNIX,socket_kind,0,descriptors) : pipe(descriptors))<0)
        return probe_errno();
    p->fds[first]=descriptors[0];p->fds[first+1]=descriptors[1];
    error=probe_flags(descriptors[0],1);
    if (!error) error=probe_flags(descriptors[1],1);
    return error;
}
static int received_ambient(struct probe *p,uint64_t operation_deadline)
{
    union {struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(int))];} control;
    struct msghdr message;
    struct iovec iov;
    struct cmsghdr *header;
    char byte='F';
    int fd,error;
    ssize_t n;
    error=pair(p,CONTROL_SEND,SOCK_DGRAM); if (error) return error;
    memset(&message,0,sizeof(message)); memset(&control,0,sizeof(control));
    iov.iov_base=&byte;iov.iov_len=1;
    message.msg_iov=&iov;message.msg_iovlen=1;
    message.msg_control=control.bytes;message.msg_controllen=sizeof(control.bytes);
    header=CMSG_FIRSTHDR(&message);
    if (!header) return EIO;
    header->cmsg_level=SOL_SOCKET;header->cmsg_type=SCM_RIGHTS;header->cmsg_len=CMSG_LEN(sizeof(int));
    fd=p->fds[AMBIENT_OPEN];memcpy(CMSG_DATA(header),&fd,sizeof(fd));
    /* A single tiny nonblocking local message; retry EINTR only within deadline. */
    {
        uint64_t now,deadline;
        if ((error=probe_now(&now))) return error;
        deadline=now+500;if (deadline>operation_deadline) deadline=operation_deadline;
        do {if ((error=probe_before(deadline))) return error;n=sendmsg(p->fds[CONTROL_SEND],&message,0);} while(n<0 && errno==EINTR);
    }
    if (n!=1) return n<0 ? probe_errno() : EIO;
    memset(&control,0,sizeof(control));message.msg_controllen=sizeof(control.bytes);message.msg_flags=0;
    /* Deliberately no MSG_CMSG_CLOEXEC, and no F_SETFD on the received FD. */
    {
        uint64_t now,deadline;
        if ((error=probe_now(&now))) return error;
        deadline=now+500;if (deadline>operation_deadline) deadline=operation_deadline;
        do {if ((error=probe_before(deadline))) return error;n=recvmsg(p->fds[CONTROL_RECEIVE],&message,0);} while(n<0 && errno==EINTR);
    }
    if (n<0) return probe_errno();
    header=CMSG_FIRSTHDR(&message);
    /* Adopt a recognizable installed right before refusing the envelope. A
     * malformed/truncated envelope may hide additional ownership; never claim
     * complete cleanup for that uncertain case. No arbitrary-FD scanning. */
    if (header && header->cmsg_level==SOL_SOCKET && header->cmsg_type==SCM_RIGHTS &&
        header->cmsg_len==CMSG_LEN(sizeof(int))) {
        memcpy(&fd,CMSG_DATA(header),sizeof(fd));
        if (fd>=0) p->fds[AMBIENT_RECEIVED]=fd;
    }
    if (n!=1 || byte!='F' || (message.msg_flags&(MSG_CTRUNC|MSG_TRUNC))) {
        p->unknown_rights_cleanup=1;return EPROTO;
    }
    if (!header || header->cmsg_level!=SOL_SOCKET || header->cmsg_type!=SCM_RIGHTS ||
        header->cmsg_len!=CMSG_LEN(sizeof(int)) || CMSG_NXTHDR(&message,header)) {
        p->unknown_rights_cleanup=1;return EPROTO;
    }
    memcpy(&fd,CMSG_DATA(header),sizeof(fd));
    if (fd<0) {p->unknown_rights_cleanup=1;return EPROTO;}
    p->fds[AMBIENT_RECEIVED]=fd;
    error=fcntl(fd,F_GETFD);
    if (error<0) return probe_errno();
    p->ambient_received_clear=(error&FD_CLOEXEC)==0;
    return p->ambient_received_clear ? 0 : EINVAL;
}
static int record_open(struct probe *p,enum role role,const char *name)
{
    char path[208];int n=snprintf(path,sizeof(path),"%s/%s",p->directory,name);
    int fd;
    if (n<0 || (size_t)n>=sizeof(path)) return ENAMETOOLONG;
    fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if (fd<0) return probe_errno();
    p->fds[role]=fd;
    if (fchmod(fd,0600)<0) return probe_errno();
    return 0;
}
static void check_wait(struct probe *p)
{
    struct owned_child *child=&p->child;
    uint64_t now=0;
    uint64_t until;
    int error;
    if (!child->live) return;
    error=probe_now(&now);
    if (error) {child->wait_error=error;fail(p,"wait_clock",error);return;}
    until=now+100;
    for (;;) {
        int status=0;
        pid_t got=waitpid(child->pid,&status,WNOHANG);
        if (got==0) return;
        if (got==child->pid) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                child->live=0;child->reaped=1;child->status=status;return;
            }
            fail(p,"nonterminal_wait",EPROTO);return;
        }
        error=got<0 ? probe_errno() : ECHILD;
        if (error==EINTR) {
            error=probe_before(until);
            if (!error) continue;
            child->wait_error=error;fail(p,"wait_eintr_deadline",error);return;
        }
        child->live=0;child->lost=1;child->wait_error=error;
        fail(p,"wait_ownership_lost",error);return;
    }
}
static void wait_until(struct probe *p,uint64_t deadline)
{
    while (p->child.live) {
        int error;
        check_wait(p);
        if (!p->child.live) return;
        error=probe_before(deadline); if (error) return;
        if (poll(NULL,0,10)<0 && errno!=EINTR) {fail(p,"wait_poll",probe_errno());return;}
    }
}
static void cleanup_child(struct probe *p)
{
    uint64_t now;
    int error;
    check_wait(p);
    if (!p->child.live) return;
    if (kill(p->child.pid,SIGTERM)==0) p->child.term_sent=1;
    else if (errno!=ESRCH) fail(p,"term_owned_child",probe_errno());
    error=probe_now(&now);
    if (error) fail(p,"term_clock",error);
    else wait_until(p,now+250);
    check_wait(p);
    if (!p->child.live) return;
    if (kill(p->child.pid,SIGKILL)==0) p->child.kill_sent=1;
    else if (errno!=ESRCH) fail(p,"kill_owned_child",probe_errno());
    error=probe_now(&now);
    if (error) fail(p,"kill_clock",error);
    else wait_until(p,now+1500);
    if (p->child.live) fail(p,"reap_deadline",ETIMEDOUT);
}
static int parse_report(struct probe *p)
{
    char text[PROBE_CHANNEL_CAP+1],*tokens[9],*cursor;
    size_t i,n=p->channels[2].count;
    uint32_t fields[6];
    if (!n || n>PROBE_CHANNEL_CAP || p->channels[2].bytes[n-1]!='\n') return EPROTO;
    memcpy(text,p->channels[2].bytes,n);text[n-1]=0;text[n]=0;
    if (strlen(text)!=n-1) return EPROTO;
    cursor=text;
    for (i=0;i<9;i++) {
        char *space;
        if (!*cursor) return EPROTO;
        tokens[i]=cursor;space=strchr(cursor,' ');
        if (i<8) {if (!space) return EPROTO;*space=0;cursor=space+1;}
        else if (space) return EPROTO;
    }
    if (strcmp(tokens[0],"CHONKFD/1") || strcmp(tokens[1],"RESULT") || strcmp(tokens[2],p->nonce)) return EPROTO;
    for (i=0;i<6;i++) if (probe_u32(tokens[3+i],&fields[i])) return EPROTO;
    if (!fields[0] || fields[0]>INT32_MAX || fields[0]!=(uint32_t)p->child.pid || fields[1]!=p->bound || fields[2]>15u || fields[3]>p->bound-4u || fields[5]>INT32_MAX) return EPROTO;
    p->report.pid=fields[0];p->report.bound=fields[1];p->report.low=fields[2];
    p->report.extra=fields[3];p->report.bits=fields[4];p->report.first_errno=fields[5];p->report.valid=1;
    return 0;
}
static void drain(struct probe *p,size_t index)
{
    const enum role roles[3]={OUTPUT_PARENT,ERROR_PARENT,STATUS_PARENT};
    struct channel *channel=&p->channels[index];
    unsigned char bytes[PROBE_CHANNEL_CAP+1];
    size_t work;
    if (channel->eof || p->fds[roles[index]]<0) return;
    for (work=0;work<4;work++) {
        ssize_t n=read(p->fds[roles[index]],bytes,sizeof(bytes));
        if (n>0) {
            size_t kept=(size_t)n;
            if (kept>PROBE_CHANNEL_CAP-channel->count) {
                kept=PROBE_CHANNEL_CAP-channel->count;channel->overflow=1;
            }
            memcpy(channel->bytes+channel->count,bytes,kept);channel->count+=kept;
            if (channel->overflow) {fail(p,"output_saturation",EOVERFLOW);return;}
        } else if (!n) {channel->eof=1;return;}
        else if (errno==EINTR) continue;
        else if (errno==EAGAIN || errno==EWOULDBLOCK) return;
        else {fail(p,"output_read",probe_errno());return;}
    }
}
static void exchange(struct probe *p,uint64_t deadline)
{
    char input[96],expected[96];
    size_t length;
    int error;
    (void)snprintf(input,sizeof(input),"CHONKFD/1 IN %s\n",p->nonce);length=strlen(input);
    while (1) {
        struct pollfd fds[4];size_t i;
        error=probe_before(deadline);if (error) {fail(p,"exchange_deadline",error);break;}
        if (p->fds[INPUT_PARENT]>=0) {
            ssize_t n=write(p->fds[INPUT_PARENT],input+p->input_sent,length-p->input_sent);
            if (n>0) p->input_sent+=(size_t)n;
            else if (!n) {fail(p,"input_zero_write",EIO);break;}
            else if (errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK) {fail(p,"input_write",probe_errno());break;}
            if (p->input_sent==length) retire(p,INPUT_PARENT);
        }
        for (i=0;i<3;i++) drain(p,i);
        check_wait(p);
        if (p->error_count || p->error_overflow) break;
        p->all_eof=p->channels[0].eof && p->channels[1].eof && p->channels[2].eof;
        if (p->all_eof && !p->child.live) break;
        fds[0].fd=p->fds[INPUT_PARENT];fds[0].events=POLLOUT;fds[0].revents=0;
        fds[1].fd=p->channels[0].eof ? -1 : p->fds[OUTPUT_PARENT];fds[1].events=POLLIN;fds[1].revents=0;
        fds[2].fd=p->channels[1].eof ? -1 : p->fds[ERROR_PARENT];fds[2].events=POLLIN;fds[2].revents=0;
        fds[3].fd=p->channels[2].eof ? -1 : p->fds[STATUS_PARENT];fds[3].events=POLLIN;fds[3].revents=0;
        if (poll(fds,4,10)<0 && errno!=EINTR) {fail(p,"exchange_poll",probe_errno());break;}
    }
    (void)snprintf(expected,sizeof(expected),"CHONKFD/1 OUT %s\n",p->nonce);
    p->exact_out=p->channels[0].count==strlen(expected) && !memcmp(p->channels[0].bytes,expected,strlen(expected));
    (void)snprintf(expected,sizeof(expected),"CHONKFD/1 ERR %s\n",p->nonce);
    p->exact_err=p->channels[1].count==strlen(expected) && !memcmp(p->channels[1].bytes,expected,strlen(expected));
    if (p->input_sent!=length || !p->exact_out || !p->exact_err || !p->all_eof) fail(p,"stdio_roundtrip",EPROTO);
    error=parse_report(p);if (error) fail(p,"status_frame",error);
    else if (p->report.low!=15 || p->report.extra || p->report.bits || p->report.first_errno) fail(p,"child_boundary_failure",p->report.first_errno ? (int)p->report.first_errno : EPROTO);
}
static void runtime(struct probe *p,uint64_t deadline)
{
    const char *args[6],*environment[2]={"LC_ALL=C",NULL};
    char bound[24];
    struct chonk_spawn_request_v1 request;
    int error,flags;
    size_t role;
    if (uname(&p->name)<0) {fail(p,"uname",probe_errno());return;}
    p->uname_checked=1;
    if (strcmp(p->name.sysname,"Darwin") || strcmp(p->name.machine,"arm64")) {fail(p,"uname_platform",EINVAL);return;}
    if (getrlimit(RLIMIT_NOFILE,&p->limit)<0) {fail(p,"getrlimit",probe_errno());return;}
    p->limit_checked=1;
    if (p->limit.rlim_cur==RLIM_INFINITY || p->limit.rlim_cur<=3 || p->limit.rlim_cur>PROBE_SCAN_CAP) {fail(p,"fdlimit",EOVERFLOW);return;}
    p->bound=(uint32_t)p->limit.rlim_cur;
    error=pair(p,INPUT_CHILD,0);if (!error) error=pair(p,OUTPUT_PARENT,0);
    if (!error) error=pair(p,ERROR_PARENT,0);
    if (!error) error=pair(p,STATUS_PARENT,SOCK_STREAM);
    if (error) {fail(p,"selected_endpoints",error);return;}
    /* Real open-to-fcntl interval intentionally remains unfinished at spawn. */
    p->fds[AMBIENT_OPEN]=open("/dev/null",O_RDONLY);
    if (p->fds[AMBIENT_OPEN]<0) {fail(p,"ambient_open",probe_errno());return;}
    flags=fcntl(p->fds[AMBIENT_OPEN],F_GETFD);
    if (flags<0) {fail(p,"ambient_open_flags",probe_errno());return;}
    p->ambient_open_clear=(flags&FD_CLOEXEC)==0;
    if (!p->ambient_open_clear) {fail(p,"ambient_open_cloexec",EINVAL);return;}
    error=received_ambient(p,deadline);if (error) {fail(p,"ambient_receive",error);return;}
    for (role=0;role<ROLE_COUNT;role++) if (p->fds[role]>=0 && (uint32_t)p->fds[role]>=p->bound) {fail(p,"owned_fd_outside_scan",EOVERFLOW);return;}
    memset(&request,0,sizeof(request));request.version=CHONK_SPAWN_VERSION;
    (void)snprintf(bound,sizeof(bound),"%u",p->bound);
    args[0]=PROBE_CHILD;args[1]="--nonce";args[2]=p->nonce;args[3]="--scan-limit";args[4]=bound;args[5]=NULL;
    request.executable=PROBE_CHILD;request.argv=args;request.argc=5;request.envp=environment;request.envc=1;
    request.source_fds[0]=p->fds[INPUT_CHILD];request.source_fds[1]=p->fds[OUTPUT_CHILD];request.source_fds[2]=p->fds[ERROR_CHILD];request.source_fds[3]=p->fds[STATUS_CHILD];
    p->ambient_held=1;p->spawn_called=1;
    p->spawn_return=chonk_darwin_spawn_v1(&request,&p->spawn);
    /* Ownership transfers even when local post-spawn destruction returned error. */
    if (p->spawn.child_created && p->spawn.pid>0) {p->child.pid=(pid_t)p->spawn.pid;p->child.live=1;}
    else if (p->spawn.child_created || p->spawn.pid) fail(p,"invalid_created_child",EPROTO);
    if (p->spawn_return) fail(p,"managed_spawn",p->spawn_return);
    if (!p->spawn.child_created || !p->child.live) {fail(p,"spawn_no_owned_child",EPROTO);return;}
    retire(p,INPUT_CHILD);retire(p,OUTPUT_CHILD);retire(p,ERROR_CHILD);retire(p,STATUS_CHILD);
    /* Keep original ambient FDs alive through the completed call, then retire. */
    retire(p,AMBIENT_OPEN);retire(p,AMBIENT_RECEIVED);retire(p,CONTROL_SEND);retire(p,CONTROL_RECEIVE);
    if (p->spawn_return || p->error_count) return;
    if (p->spawn.version!=CHONK_SPAWN_VERSION || p->spawn.primary_error || p->spawn.spawn_error ||
        p->spawn.attr_destroy_error || p->spawn.actions_destroy_error || p->spawn.fd_close_error || p->spawn.fd_close_failures ||
        p->spawn.duplicate_count!=4 || p->spawn.requested_flags!=PROBE_REQUIRED_FLAGS || p->spawn.observed_flags!=PROBE_REQUIRED_FLAGS) {
        fail(p,"spawn_result_contract",EPROTO);return;
    }
    exchange(p,deadline);
}
static void transcript(struct probe *p,enum role role,const struct channel *channel,uint64_t deadline)
{
    int error=probe_write(p->fds[role],channel->bytes,channel->count,deadline);
    if (error) fail(p,"transcript_write",error);
    if (fsync(p->fds[role])<0) fail(p,"transcript_sync",probe_errno());
    retire(p,role);
}
static int final_record(struct probe *p,uint64_t deadline)
{
    char text[12288];size_t n=0,i;
    int count,error;
    uint64_t soft=p->limit_checked && p->limit.rlim_cur!=RLIM_INFINITY ? (uint64_t)p->limit.rlim_cur : 0;
    uint64_t hard=p->limit_checked && p->limit.rlim_max!=RLIM_INFINITY ? (uint64_t)p->limit.rlim_max : 0;
#define APPEND(...) do {count=snprintf(text+n,sizeof(text)-n,__VA_ARGS__);if (count<0 || (size_t)count>=sizeof(text)-n) return EOVERFLOW;n+=(size_t)count;} while (0)
    APPEND("{\n\"schema\":1,\"nonce\":\"%s\",\"child_path\":\"%s\",",p->nonce,PROBE_CHILD);
    /* Untrusted/free-form uname strings are compared, never interpolated. */
    APPEND("\"darwin_arm64\":%s,\"limit_checked\":%s,\"soft_limit\":%llu,\"hard_limit\":%llu,\"soft_infinite\":%s,\"hard_infinite\":%s,\"scan_bound\":%u,",
           p->uname_checked && !strcmp(p->name.sysname,"Darwin") && !strcmp(p->name.machine,"arm64") ? "true":"false",p->limit_checked?"true":"false",(unsigned long long)soft,(unsigned long long)hard,p->limit_checked && p->limit.rlim_cur==RLIM_INFINITY?"true":"false",p->limit_checked && p->limit.rlim_max==RLIM_INFINITY?"true":"false",p->bound);
    APPEND("\"ambient_open_noncloexec\":%s,\"ambient_received_noncloexec\":%s,\"ambient_held_through_spawn\":%s,\"unknown_rights_cleanup\":%s,\"spawn_called\":%s,\"spawn_return\":%d,",p->ambient_open_clear?"true":"false",p->ambient_received_clear?"true":"false",p->ambient_held?"true":"false",p->unknown_rights_cleanup?"true":"false",p->spawn_called?"true":"false",p->spawn_return);
    APPEND("\"spawn\":{\"version\":%u,\"stage\":%u,\"pid\":%d,\"primary_error\":%d,\"spawn_error\":%d,\"attr_destroy_error\":%d,\"actions_destroy_error\":%d,\"fd_close_error\":%d,\"fd_close_failures\":%u,\"requested_flags\":%u,\"observed_flags\":%u,\"child_created\":%u,\"duplicate_count\":%u},",p->spawn.version,p->spawn.stage,p->spawn.pid,p->spawn.primary_error,p->spawn.spawn_error,p->spawn.attr_destroy_error,p->spawn.actions_destroy_error,p->spawn.fd_close_error,p->spawn.fd_close_failures,p->spawn.requested_flags,p->spawn.observed_flags,p->spawn.child_created,p->spawn.duplicate_count);
    APPEND("\"input_sent\":%zu,\"stdout_bytes\":%zu,\"stderr_bytes\":%zu,\"status_bytes\":%zu,\"stdout_exact\":%s,\"stderr_exact\":%s,\"all_output_eof\":%s,\"output_overflow\":%s,",p->input_sent,p->channels[0].count,p->channels[1].count,p->channels[2].count,p->exact_out?"true":"false",p->exact_err?"true":"false",p->all_eof?"true":"false",p->channels[0].overflow||p->channels[1].overflow||p->channels[2].overflow?"true":"false");
    APPEND("\"child_report\":{\"valid\":%s,\"pid\":%u,\"scan_bound\":%u,\"low_mask\":%u,\"extra_fds\":%u,\"failure_bits\":%u,\"first_errno\":%u},",p->report.valid?"true":"false",p->report.pid,p->report.bound,p->report.low,p->report.extra,p->report.bits,p->report.first_errno);
    APPEND("\"wait\":{\"reaped\":%s,\"lost\":%s,\"raw_status\":%d,\"error\":%d,\"term_sent\":%s,\"kill_sent\":%s},\"error_overflow\":%s,\"errors\":[",p->child.reaped?"true":"false",p->child.lost?"true":"false",p->child.reaped?p->child.status:-1,p->child.wait_error,p->child.term_sent?"true":"false",p->child.kill_sent?"true":"false",p->error_overflow?"true":"false");
    for (i=0;i<p->error_count;i++) APPEND("%s{\"stage\":\"%s\",\"errno\":%d}",i?",":"",p->errors[i].stage,p->errors[i].error);
    APPEND("]}\n");
#undef APPEND
    error=probe_write(p->fds[RESULT],text,n,deadline);if (error) return error;
    if (fsync(p->fds[RESULT])<0) return probe_errno();
    return 0;
}
static int final_failure(const char *stage,int error)
{
    char line[128];int n=snprintf(line,sizeof(line),"CHONKFD/1 PARENT_FAILURE %s %d\n",stage,error);
    uint64_t now;
    if (n>0 && (size_t)n<sizeof(line) && !probe_now(&now)) {
        /* Best-effort fixed diagnostic; external capture/exit is required. */
        if (!probe_flags(2,0)) (void)probe_write(2,line,(size_t)n,now+100);
    }
    return 1;
}
int main(int argc,char **argv)
{
    struct probe p;
    struct stat metadata;
    uint64_t now,deadline,final_deadline;
    size_t i;
    int error;
    memset(&p,0,sizeof(p));for (i=0;i<ROLE_COUNT;i++) p.fds[i]=-1;
    if (argc!=3 || strcmp(argv[1],"--nonce") || !probe_nonce(argv[2])) return 64;
    p.nonce=argv[2];
    error=probe_ignore_pipe();if (error) return final_failure("sigpipe",error);
    error=probe_now(&now);if (error) return final_failure("clock",error);
    deadline=now+PROBE_OPERATION_MS;
    (void)umask(0077);
    (void)snprintf(p.directory,sizeof(p.directory),"%s%s",PROBE_PREFIX,p.nonce);
    if (mkdir(p.directory,0700)<0) return final_failure("private_directory",probe_errno());
    if (lstat(p.directory,&metadata)<0) return final_failure("private_stat",probe_errno());
    if (!S_ISDIR(metadata.st_mode) || metadata.st_uid!=geteuid() || (metadata.st_mode&07777)!=0700) return final_failure("private_mode",EINVAL);
    error=record_open(&p,RESULT,"result.json");
    if (!error) error=record_open(&p,LOG_OUTPUT,"stdout.bin");
    if (!error) error=record_open(&p,LOG_ERROR,"stderr.bin");
    if (!error) error=record_open(&p,LOG_STATUS,"status.bin");
    if (error) fail(&p,"record_open",error);
    else runtime(&p,deadline);
    cleanup_child(&p);
    if (p.child.live || p.child.lost || p.unknown_rights_cleanup || (p.child.pid>0 && !p.child.reaped)) fail(&p,"cleanup_incomplete",ECHILD);
    if (p.child.reaped && (!WIFEXITED(p.child.status) || WEXITSTATUS(p.child.status)!=0)) fail(&p,"child_exit",EPROTO);
    if (p.child.term_sent || p.child.kill_sent) fail(&p,"child_forced_stop",ETIMEDOUT);
    error=probe_now(&now);if (error) {fail(&p,"final_clock",error);now=deadline;}
    final_deadline=now+500;
    if (p.fds[LOG_OUTPUT]>=0) transcript(&p,LOG_OUTPUT,&p.channels[0],final_deadline);
    if (p.fds[LOG_ERROR]>=0) transcript(&p,LOG_ERROR,&p.channels[1],final_deadline);
    if (p.fds[LOG_STATUS]>=0) transcript(&p,LOG_STATUS,&p.channels[2],final_deadline);
    for (i=0;i<ROLE_COUNT;i++) if (i!=RESULT) retire(&p,(enum role)i);
    if (p.fds[RESULT]<0) return final_failure("result_missing",EIO);
    error=final_record(&p,final_deadline);
    if (error) {retire(&p,RESULT);return final_failure("result_write_sync",error);}
    {
        int fd=p.fds[RESULT];p.fds[RESULT]=-1;
        if (close(fd)<0) return final_failure("result_close",probe_errno());
    }
    return p.error_count || p.error_overflow ? 1 : 0;
}
