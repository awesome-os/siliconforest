#ifndef __APPLE__
#error Darwin route adapter only
#endif
#ifndef DLE_ROUTE_PROTOTYPE_ANALYSIS_ONLY
#error Unadmitted prototype: ACK filtering, uncertain ADD and DELETE ownership unresolved
#endif
#include "owned_routes.h"
#include "route_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <net/if.h>
#include <net/route.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void) {
  struct timespec t;
  if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
  return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static int ready(int fd, int writing, uint64_t deadline) {
  if(fd<0 || fd>=FD_SETSIZE) {errno=EINVAL; return -1;}
  for(;;) {
    uint64_t t=now_ms();
    if(!t || t>=deadline) {errno=ETIMEDOUT; return -1;}
    fd_set bits;FD_ZERO(&bits);FD_SET(fd,&bits);
    uint64_t left=deadline-t;
    struct timeval delay={(time_t)(left/1000),(suseconds_t)((left%1000)*1000)};
    int n=select(fd+1,writing?NULL:&bits,writing?&bits:NULL,NULL,&delay);
    if(n>0) return 0;
    if(n<0 && errno==EINTR) continue;
    if(!n) errno=ETIMEDOUT;
    return -1;
  }
}
static int exchange(struct dle_routes *r, uint8_t type, uint32_t dst,
                     struct dle_route_view *view) {
  if(r->fd<0 || r->sequence==INT_MAX) {errno=EINVAL; return -1;}
  uint8_t request[160], response[4096];size_t length=0;
  uint32_t seq=(uint32_t)++r->sequence;
  if(dle_route_encode(request,sizeof(request),&length,r->pid,seq,type,dst,
                      0x0a000202,r->index)) {errno=EINVAL; return -1;}
  uint64_t started=now_ms();
  if(!started) {errno=EIO; return -1;}
  uint64_t deadline=started+1500;
  for(;;) {
    if(ready(r->fd,1,deadline)) return -1;
    ssize_t n=write(r->fd,request,length);
    if(n<0 && (errno==EAGAIN || errno==EINTR)) continue;
    /* XNU routes the error acknowledgement and also returns its errno to
     * write(). Never infer success from write alone, including these errors. */
    if(n<0 && (errno==ESRCH || errno==EEXIST)) break;
    if(n!=(ssize_t)length) {if(n>=0) errno=EIO; return -1;}
    break;
  }
  if(r->record) r->record("request",request,length,r->context);
  for(unsigned messages=0;messages<64;messages++) {
    if(ready(r->fd,0,deadline)) return -1;
    ssize_t n=recv(r->fd,response,sizeof(response),0);
    if(n<0 && (errno==EAGAIN || errno==EINTR)) {messages--;continue;}
    if(n<=0) {if(!n) errno=EIO;return -1;}
    int result=dle_route_decode(response,(size_t)n,r->pid,seq,type,view);
    if(result==1) continue;
    if(r->record) r->record("response",response,(size_t)n,r->context);
    if(result) {errno=EPROTO;return -1;}
    return 0;
  }
  errno=EOVERFLOW;return -1;
}
static int matches(const struct dle_routes *r, uint32_t dst,
                    const struct dle_route_view *v) {
  const uint32_t required=RTF_UP|RTF_HOST|RTF_GATEWAY|RTF_STATIC;
  const uint32_t forbidden=RTF_REJECT|RTF_BLACKHOLE|RTF_CONDEMNED|RTF_DEAD;
  return !v->error && v->have_destination && v->destination==dst &&
         v->have_gateway && v->gateway==0x0a000202 &&
         v->interface_index==r->index &&
         (v->flags&required)==required && !(v->flags&forbidden) &&
         (!v->have_netmask || v->netmask==0xffffffff);
}
int dle_routes_open(struct dle_routes *r, const char *name,
                    void (*record)(const char *,const uint8_t *,size_t,void *),
                    void *context) {
  if(!r || !name || strncmp(name,"utun",4)) {errno=EINVAL;return -1;}
  memset(r,0,sizeof(*r));r->fd=-1;
  unsigned index=if_nametoindex(name);
  if(!index || index>UINT16_MAX) {errno=ENXIO;return -1;}
  r->index=(uint16_t)index;r->pid=(uint32_t)getpid();
  r->record=record;r->context=context;
  int fd=socket(PF_ROUTE,SOCK_RAW,AF_INET);
  if(fd<0) return -1;
  int flags=fcntl(fd,F_GETFL), fds=fcntl(fd,F_GETFD);
  if(flags<0 || fds<0 || fd>=FD_SETSIZE ||
     fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0 ||
     fcntl(fd,F_SETFD,fds|FD_CLOEXEC)<0) {
    int saved=errno; (void)close(fd);errno=saved?saved:EINVAL;return -1;
  }
  r->fd=fd;return 0;
}
int dle_routes_add(struct dle_routes *r, unsigned slot, uint32_t dst) {
  if(!r || slot>=2 || !dst || dst==0x0a000202 ||
     r->entries[slot].owned || r->entries[1-slot].destination==dst) {
    errno=EINVAL;return -1;
  }
  struct dle_route_view view;
  if(exchange(r,RTM_GET,dst,&view)) return -1;
  /* Refuse even a broader existing route: this first proof starts fresh. */
  if(view.error!=ESRCH) {errno=view.error?(int)view.error:EEXIST;return -1;}
  if(exchange(r,RTM_ADD,dst,&view)) return -1;
  if(view.error) {errno=(int)view.error;return -1;}
  /* Ownership is established only by the matching successful kernel ack. */
  r->entries[slot]=(struct dle_owned_route){dst,1};
  if(exchange(r,RTM_GET,dst,&view)) return -1;
  if(!matches(r,dst,&view)) {errno=EPROTO;return -1;}
  return 0;
}
int dle_routes_close(struct dle_routes *r) {
  if(!r) {errno=EINVAL;return -1;}
  int failed=0, saved=0;
  for(unsigned i=0;i<2;i++) {
    if(!r->entries[i].owned) continue;
    uint32_t dst=r->entries[i].destination; struct dle_route_view view;
    if(exchange(r,RTM_GET,dst,&view) || !matches(r,dst,&view)) {
      failed=1;if(!saved) saved=errno?errno:EPROTO;continue;
    }
    /* Never delete a route which no longer matches the owned snapshot. */
    if(exchange(r,RTM_DELETE,dst,&view) || view.error) {
      failed=1;if(!saved) saved=errno?errno:EPROTO;continue;
    }
    r->entries[i].owned=0;
    if(exchange(r,RTM_GET,dst,&view) || view.error!=ESRCH) {
      failed=1;if(!saved) saved=errno?errno:EPROTO;
    }
  }
  int fd=r->fd;r->fd=-1;
  if(fd>=0 && close(fd)) {failed=1;if(!saved) saved=errno;}
  if(failed) errno=saved?saved:EPROTO;
  return failed?-1:0;
}
