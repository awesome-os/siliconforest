#ifndef __APPLE__
#error Darwin owned default-route adapter only
#endif
#include "native.h"
#include "route_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/route.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
static uint64_t millis(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static int wait_fd(int fd,int write_ready,uint64_t end) {
    for(;;){
        uint64_t now=millis();if(!now||now>=end){errno=ETIMEDOUT;return -1;}
        fd_set set;FD_ZERO(&set);FD_SET(fd,&set);uint64_t left=end-now;
        struct timeval timeout={(time_t)(left/1000),(suseconds_t)((left%1000)*1000)};
        int rc=select(fd+1,write_ready?NULL:&set,write_ready?&set:NULL,NULL,&timeout);
        if(rc>0)return 0;
        if(rc<0&&errno==EINTR)continue;
        if(!rc)errno=ETIMEDOUT;
        return -1;
    }
}
static void record(struct dlf_native *s,const char *kind,const uint8_t *b,size_t n) {
    if(s->record)s->record(kind,b,n,s->record_context);
}
static int inventory(struct dlf_native *s,int capture){
    int mib[6]={CTL_NET,PF_ROUTE,0,AF_INET,NET_RT_DUMP,0};size_t n=sizeof(s->dump);
    if(sysctl(mib,6,s->dump,&n,NULL,0)||n>sizeof(s->dump)){s->last_errno=errno;return -1;}
    struct dlf_route_set *out=capture?&s->baseline_routes:&s->final_routes;
    if(dlf_inventory(s->dump,n,out,&s->scratch_routes)){s->last_errno=EPROTO;errno=EPROTO;return -1;}
    for(size_t off=0;off<n;){
        size_t length=s->dump[off]|(size_t)s->dump[off+1]<<8;
        record(s,capture?"baseline_ipv4_route":"final_ipv4_route",s->dump+off,length);off+=length;
    }
    uint8_t count[4];for(unsigned i=0;i<4;i++)count[i]=(uint8_t)(out->count>>(i*8));
    record(s,capture?"baseline_ipv4_route_count":"final_ipv4_route_count",count,sizeof(count));
    if(capture){
        if(dlf_inventory_has_default(out)){s->last_errno=EEXIST;errno=EEXIST;return -1;}
        s->baseline_routes_known=1;return 0;
    }
    return !s->baseline_routes_known||!dlf_inventory_equal(&s->baseline_routes,&s->final_routes);
}
static int exchange(struct dlf_native *s,uint8_t type,const struct dlf_owner *owner,struct dlf_route *out) {
    uint8_t request[192],reply[4096];size_t length=0;
    uint32_t pid=(uint32_t)getpid();
    if(dlf_encode(request,sizeof(request),&length,pid,type,owner)){errno=EINVAL;return -1;}
    /* Protocol0 receives successful AND failed routing acknowledgements. */
    int fd=socket(PF_ROUTE,SOCK_RAW,0);if(fd<0)return -1;
    int rc=-1,flags=fcntl(fd,F_GETFL),fds=fcntl(fd,F_GETFD),saved;
    struct dlf_route result;
    if(fd>=FD_SETSIZE){errno=EINVAL;goto done;}
    if(flags<0||fds<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0||fcntl(fd,F_SETFD,fds|FD_CLOEXEC)<0)goto done;
    uint64_t start=millis();if(!start||start>UINT64_MAX-1500){errno=EIO;goto done;}
    uint64_t end=start+1500;
    record(s,type==1?"default_add_request":"default_get_request",request,length);
    for(;;){
        if(wait_fd(fd,1,end))goto done;
        ssize_t n=write(fd,request,length);
        int write_error=n<0?errno:0;
        char line[80];int chars=snprintf(line,sizeof(line),"type=%u bytes=%lld errno=%d",type,(long long)n,write_error);
        if(chars>0&&(size_t)chars<sizeof(line))record(s,"default_write_result",(const uint8_t *)line,(size_t)chars);
        errno=write_error;
        if(type==4&&n<0&&(errno==EINTR||errno==EAGAIN))continue;
        /* ESRCH/explicit ADD failures still produce an errno acknowledgement. */
        if(n<0){if(type==4&&errno==ESRCH)break;goto done;}
        if(n!=(ssize_t)length){errno=EIO;goto done;}
        break;
    }
    for(unsigned seen=0;seen<64;){
        if(wait_fd(fd,0,end))goto done;
        ssize_t n=recv(fd,reply,sizeof(reply),0);
        if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
        if(n<=0){if(!n)errno=EIO;goto done;}
        seen++;int parsed=dlf_decode(reply,(size_t)n,pid,type,&result);
        if(parsed==1)continue;
        record(s,type==1?"default_add_reply":"default_get_reply",reply,(size_t)n);
        if(parsed){errno=EPROTO;goto done;}
        rc=0;break;
    }
    if(rc)errno=EOVERFLOW;
done:
    saved=errno;
    if(close(fd)){if(!rc)saved=errno;rc=-1;}
    if(!rc)*out=result;
    else{s->last_errno=saved;errno=saved;}
    return rc;
}
static int get_default(void *c,struct dlf_route *out){return exchange(c,4,NULL,out);}
static int open_owner(void *c,struct dlf_owner *owner){
    struct dlf_native *s=c;
    if(inventory(s,1))return -1;
    if(dle_interfaces_open(&s->owned,DLF_PEER)){s->last_errno=errno;return -1;}
    struct dlf_owner o={0};o.index=(uint16_t)s->owned.index;
    memcpy(o.name,s->owned.name,sizeof(o.name));*owner=o;return 0;
}
static int add_default(void *c,const struct dlf_owner *owner){
    struct dlf_native *s=c;struct dlf_route r={0};
    if(exchange(s,1,owner,&r))return -1;
    /* ACK echoes input+DONE; it does not report the resulting binding/flags. */
    if(r.error||r.flags!=0x843||!r.have_destination||r.destination||!r.have_gateway||r.gateway!=DLF_PEER||
       !r.have_netmask||r.netmask||!r.have_ifa||r.ifa!=DLF_LOCAL||!r.have_ifp||
       r.index!=owner->index||r.ifp_index!=owner->index||memcmp(r.ifp_name,owner->name,sizeof(owner->name))){
        s->last_errno=r.error?(int)r.error:EPROTO;errno=s->last_errno;return -1;
    }
    return 0;
}
static int close_owner(void *c){
    struct dlf_native *s=c;int rc=dle_interfaces_close(&s->owned);
    if(rc)s->last_errno=errno;
    return rc;
}
static int baseline_equal(void *c){
    struct dlf_native *s=c;
    /* The owned-interface close performs actual inventory equality and peer/local
     * route absence. Never turn an unobserved/uncertain close into cleanup. */
    if(!s->owned._baseline_known||!s->owned.cleanup_complete||s->owned._descriptor_uncertain||s->owned._owned_fd>=0)return -1;
    return inventory(s,0);
}
static const struct dlf_ops operations={get_default,open_owner,add_default,close_owner,baseline_equal};
int dlf_native_init(struct dlf_native *s,int exclusive,
    void (*observe)(const char *,const uint8_t *,size_t,void *),void *context){
    if(!s){errno=EINVAL;return -1;}
    const unsigned char *bytes=(const unsigned char *)s;
    for(size_t i=0;i<sizeof(*s);i++)if(bytes[i]){errno=EALREADY;return -1;}
    memset(s,0,sizeof(*s));s->record=observe;s->record_context=context;
    if(dle_interfaces_init(&s->owned,exclusive,observe,context))return -1;
    return dlf_init(&s->lifetime,&operations,s,exclusive);
}
int dlf_native_open(struct dlf_native *s){return s?dlf_open(&s->lifetime):-1;}
int dlf_native_close(struct dlf_native *s){return s?dlf_close(&s->lifetime):-1;}
int dlf_native_fd(const struct dlf_native *s){
    return s&&s->lifetime.stage==DLF_ACTIVE&&!s->lifetime.first_error?s->owned.fd:-1;
}
