/* SPDX-License-Identifier: MIT */
#include "sc-route-source.h"
#include <CoreFoundation/CFRuntime.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <net/route.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct { unsigned refs; CFTypeRef target; SFSCNetworkNotification notify; } Callout;
typedef struct {
    CFRuntimeBase base;
    int route, queue, stop[2], bridge[2], dns, dirs[2];
    pthread_t thread; int started;
    CFSocketRef socket;
    CFRunLoopSourceRef readable;
    char path[PATH_MAX];
    uint64_t dnsHash;
} Watch;
static CFTypeID watchType;
static pthread_once_t once=PTHREAD_ONCE_INIT;

static const void *calloutRetain(const void *p) {
    Callout *c=(Callout *)p; __atomic_add_fetch(&c->refs,1,__ATOMIC_RELAXED); return p;
}
static void calloutRelease(const void *p) {
    Callout *c=(Callout *)p;
    if(!__atomic_sub_fetch(&c->refs,1,__ATOMIC_ACQ_REL)){CFRelease(c->target);free(c);}
}
static void readable(CFSocketRef socket,CFSocketCallBackType type,CFDataRef address,
                     const void *data,void *info) {
    (void)type;(void)address;(void)data;
    unsigned changes=0; unsigned char bytes[128]; ssize_t count;
    int fd=CFSocketGetNative(socket);
    while((count=read(fd,bytes,sizeof(bytes)))>0)
        for(ssize_t i=0;i<count;i++)changes|=bytes[i];
    if(!changes)return;
    Callout *c=info;
    /* The callback may remove and release its own source. Keep no borrowed
     * callout pointer across it: invalidation can release the socket context. */
    CFTypeRef target=CFRetain(c->target); SFSCNetworkNotification notify=c->notify;
    notify(target,changes); CFRelease(target);
}
static uint64_t dnsHash(const char *path) {
    FILE *f=fopen(path,"r"); if(!f)return 0;
    uint64_t h=1469598103934665603ULL; unsigned char bytes[1024]; size_t n;
    while((n=fread(bytes,1,sizeof(bytes),f)))
        for(size_t i=0;i<n;i++){h^=bytes[i];h*=1099511628211ULL;}
    fclose(f);return h;
}
static int watchFD(int queue,int fd,int filter,unsigned flags,uintptr_t tag) {
    struct kevent event;EV_SET(&event,fd,filter,EV_ADD|EV_CLEAR,flags,0,(void *)tag);
    return kevent(queue,&event,1,NULL,0,NULL);
}
static void watchDNS(Watch *w) {
    if(w->dns>=0)close(w->dns);
    w->dns=open(w->path,O_EVTONLY|O_CLOEXEC);
    if(w->dns>=0 && watchFD(w->queue,w->dns,EVFILT_VNODE,
            NOTE_WRITE|NOTE_EXTEND|NOTE_DELETE|NOTE_RENAME|NOTE_ATTRIB,2)<0){close(w->dns);w->dns=-1;}
}
static int routeChanged(int fd) {
    unsigned char bytes[16384];ssize_t n;int changed=0;
    while((n=read(fd,bytes,sizeof(bytes)))>0){
        size_t at=0;
        while(at+4<=(size_t)n){
            uint16_t len;memcpy(&len,bytes+at,2);
            if(len<4||at+len>(size_t)n)break;
            if(bytes[at+2]==RTM_VERSION)switch(bytes[at+3]){
                case RTM_ADD:case RTM_DELETE:case RTM_CHANGE:
                case RTM_IFINFO:case RTM_NEWADDR:case RTM_DELADDR:changed=1;break;
                default:break; /* A route lookup's own MISS is not a change. */
            }
            at+=len;
        }
    }
    return changed;
}
static void *worker(void *info) {
    Watch *w=info;unsigned char first=SF_SC_ROUTE_CHANGED|SF_SC_DNS_CHANGED;
    (void)send(w->bridge[1],&first,1,MSG_DONTWAIT);
    for(;;){
        struct kevent events[8];int n=kevent(w->queue,NULL,0,events,8,NULL);
        if(n<0){if(errno==EINTR)continue;break;}
        unsigned char changes=0;int dnsEvent=0;
        for(int i=0;i<n;i++){
            uintptr_t tag=(uintptr_t)events[i].udata;
            if(tag==128)return NULL;
            if(tag==1 && routeChanged(w->route))changes|=SF_SC_ROUTE_CHANGED;
            if(tag==2)dnsEvent=1;
        }
        if(dnsEvent){
            uint64_t now=dnsHash(w->path);
            if(now!=w->dnsHash){w->dnsHash=now;changes|=SF_SC_DNS_CHANGED;}
            /* Atomic file replacement must not leave a watcher on the old inode. */
            watchDNS(w);
        }
        if(changes)(void)send(w->bridge[1],&changes,1,MSG_DONTWAIT);
    }
    return NULL;
}
static void finalize(CFTypeRef value) {
    Watch *w=(Watch *)value;
    if(w->started){char stop=1;(void)write(w->stop[1],&stop,1);pthread_join(w->thread,NULL);}
    if(w->socket){CFSocketInvalidate(w->socket);w->bridge[0]=-1;}
    if(w->readable)CFRelease(w->readable);
    if(w->socket)CFRelease(w->socket);
    int fds[]={w->route,w->queue,w->stop[0],w->stop[1],w->bridge[0],w->bridge[1],w->dns,w->dirs[0],w->dirs[1]};
    for(unsigned i=0;i<sizeof(fds)/sizeof(fds[0]);i++)if(fds[i]>=0)close(fds[i]);
}
static void initialize(void) {
    static const CFRuntimeClass cls={.version=0,.className="SFSCNetworkWatch",.finalize=finalize};
    watchType=_CFRuntimeRegisterClass(&cls);
}
static void schedule(void *value,CFRunLoopRef loop,CFStringRef mode) {
    Watch *w=value;CFRunLoopAddSource(loop,w->readable,mode);
}
static void cancel(void *value,CFRunLoopRef loop,CFStringRef mode) {
    Watch *w=value;CFRunLoopRemoveSource(loop,w->readable,mode);
}
static void perform(void *value){(void)value;}
static void watchDirectory(Watch *w,int slot,const char *path) {
    char parent[PATH_MAX];if(strlen(path)>=sizeof(parent))return;strcpy(parent,path);
    char *slash=strrchr(parent,'/');if(!slash)return;slash[slash==parent?1:0]=0;
    w->dirs[slot]=open(parent,O_EVTONLY|O_CLOEXEC);
    if(w->dirs[slot]>=0&&watchFD(w->queue,w->dirs[slot],EVFILT_VNODE,
            NOTE_WRITE|NOTE_DELETE|NOTE_RENAME,2)<0){close(w->dirs[slot]);w->dirs[slot]=-1;}
}
CFRunLoopSourceRef SFSCNetworkSourceCreate(CFAllocatorRef allocator,CFTypeRef target,
                                         CFIndex order,SFSCNetworkNotification notify,const char *path) {
    if(!target||!notify)return NULL;
    pthread_once(&once,initialize);if(!watchType)return NULL;
    Watch *w=(Watch *)_CFRuntimeCreateInstance(allocator,watchType,sizeof(Watch)-sizeof(CFRuntimeBase),NULL);
    if(!w)return NULL;
    memset((char *)w+sizeof(CFRuntimeBase),0,sizeof(Watch)-sizeof(CFRuntimeBase));
    w->route=w->queue=w->stop[0]=w->stop[1]=w->bridge[0]=w->bridge[1]=w->dns=w->dirs[0]=w->dirs[1]=-1;
    const char *dns=path?path:"/etc/resolv.conf";
    if(strlen(dns)>=sizeof(w->path))goto fail;strcpy(w->path,dns);
    w->route=socket(PF_ROUTE,SOCK_RAW,0);w->queue=kqueue();
    if(w->route<0||w->queue<0||pipe(w->stop)||socketpair(AF_UNIX,SOCK_DGRAM,0,w->bridge))goto fail;
    int fds[]={w->route,w->queue,w->stop[0],w->stop[1],w->bridge[0],w->bridge[1]};
    for(unsigned i=0;i<sizeof(fds)/sizeof(fds[0]);i++)fcntl(fds[i],F_SETFD,FD_CLOEXEC);
    fcntl(w->route,F_SETFL,fcntl(w->route,F_GETFL)|O_NONBLOCK);
    for(int i=0;i<2;i++)fcntl(w->bridge[i],F_SETFL,fcntl(w->bridge[i],F_GETFL)|O_NONBLOCK);
    if(watchFD(w->queue,w->route,EVFILT_READ,0,1)<0||watchFD(w->queue,w->stop[0],EVFILT_READ,0,128)<0)goto fail;
    watchDNS(w);watchDirectory(w,0,w->path);
    char resolved[PATH_MAX];if(realpath(w->path,resolved)&&strcmp(resolved,w->path))watchDirectory(w,1,resolved);
    w->dnsHash=dnsHash(w->path);
    Callout *c=calloc(1,sizeof(*c));if(!c)goto fail;c->refs=1;c->target=CFRetain(target);c->notify=notify;
    CFSocketContext context={0,c,calloutRetain,calloutRelease,NULL};
    w->socket=CFSocketCreateWithNative(allocator,w->bridge[0],kCFSocketReadCallBack,readable,&context);
    calloutRelease(c);if(!w->socket)goto fail;
    CFSocketSetSocketFlags(w->socket,CFSocketGetSocketFlags(w->socket)|
                         kCFSocketCloseOnInvalidate|kCFSocketAutomaticallyReenableReadCallBack);
    w->readable=CFSocketCreateRunLoopSource(allocator,w->socket,order);if(!w->readable)goto fail;
    if(pthread_create(&w->thread,NULL,worker,w))goto fail;w->started=1;
    CFRunLoopSourceContext source={0,w,CFRetain,CFRelease,NULL,NULL,NULL,schedule,cancel,perform};
    CFRunLoopSourceRef result=CFRunLoopSourceCreate(allocator,order,&source);CFRelease(w);return result;
fail:
    CFRelease(w);return NULL;
}
