#ifndef __APPLE__
#error Darwin query-only route adapter
#endif
#include "query_routes.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t query_now(void) {
  struct timespec t;
  if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
  return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static int query_wait(int fd, int writing, uint64_t end) {
  for(;;) {
    uint64_t now=query_now();
    if(!now || now>=end) {errno=ETIMEDOUT;return -1;}
    fd_set bits;FD_ZERO(&bits);FD_SET(fd,&bits);
    uint64_t n=end-now;
    struct timeval delay={(time_t)(n/1000),(suseconds_t)((n%1000)*1000)};
    int rc=select(fd+1,writing?NULL:&bits,writing?&bits:NULL,NULL,&delay);
    if(rc>0) return 0;
    if(rc<0 && errno==EINTR) continue;
    if(!rc) errno=ETIMEDOUT;
    return -1;
  }
}
int dle_route_query(uint32_t dst, struct dle_route_view *out,
                     void (*record)(const char *,const uint8_t *,size_t,void *),
                     void *context) {
  if(!out || !dst) {errno=EINVAL;return -1;}
  /* Protocol0 is necessary: this XNU sends failedGET replies with family0. */
  int fd=socket(PF_ROUTE,SOCK_RAW,0);
  if(fd<0) return -1;
  int result=-1,saved=0,flags=fcntl(fd,F_GETFL),fds=fcntl(fd,F_GETFD);
  struct dle_route_view view;
  uint8_t request[160],reply[4096];size_t length=0;
  uint32_t pid=(uint32_t)getpid();
  if(fd>=FD_SETSIZE) {errno=EINVAL;goto done;}
  if(flags<0 || fds<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0 ||
     fcntl(fd,F_SETFD,fds|FD_CLOEXEC)<0) goto done;
  /* Fresh owned socket, single query: no oldPID/sequence frame can remain. */
  if(dle_route_encode(request,sizeof(request),&length,pid,1,4,dst,0,0)) {
    errno=EINVAL;goto done;
  }
  uint64_t start=query_now();
  if(!start) {errno=EIO;goto done;}
  uint64_t deadline=start+1500;
  for(;;) {
    if(query_wait(fd,1,deadline)) goto done;
    ssize_t n=write(fd,request,length);
    if(n<0 && (errno==EAGAIN || errno==EINTR)) continue;
    if(n<0 && errno==ESRCH) break;
    if(n!=(ssize_t)length) {if(n>=0)errno=EIO;goto done;}
    break;
  }
  if(record) record("get_request",request,length,context);
  for(unsigned messages=0;messages<64;) {
    if(query_wait(fd,0,deadline)) goto done;
    ssize_t n=recv(fd,reply,sizeof(reply),0);
    if(n<0 && (errno==EAGAIN || errno==EINTR)) continue;
    if(n<=0) {if(!n)errno=EIO;goto done;}
    messages++;
    int parsed=dle_route_decode(reply,(size_t)n,pid,1,4,&view);
    if(parsed==1) continue;
    if(record) record("get_response",reply,(size_t)n,context);
    if(parsed) {errno=EPROTO;goto done;}
    result=0;break;
  }
  if(result) errno=EOVERFLOW;
done:
  saved=errno;
  if(close(fd)) {if(!result)saved=errno;result=-1;}
  if(!result) *out=view;
  else errno=saved;
  return result;
}
