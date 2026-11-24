#ifndef __APPLE__
#error Darwin owned interface adapter only
#endif
#include "interface_internal.h"
#include "../route/query_routes.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <net/route.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <time.h>
#include <unistd.h>

_Static_assert(IFNAMSIZ==DLE_INTERFACE_NAME,"interface name capacity");
_Static_assert((RTF_UP|RTF_HOST|RTF_DONE)==0x45,"required route flags");
_Static_assert((RTF_UP|RTF_HOST|RTF_DONE|RTF_IFREF)==0x04000045,"allowed route flags");

static int identity_name(const char *name) {
  size_t n=strnlen(name,DLE_INTERFACE_NAME);
  if(n<5 || n==DLE_INTERFACE_NAME || memcmp(name,"utun",4)) return 0;
  for(size_t i=4;i<n;i++) if(name[i]<'0' || name[i]>'9') return 0;
  return 1;
}
static int inventory(void *ctx,struct dle_interfaces *s,int capture) {
  (void)ctx;
  struct if_nameindex *list=if_nameindex();
  if(!list) return -1;
  struct dle_interface_identity current[DLE_INTERFACE_LIMIT];
  memset(current,0,sizeof(current));size_t count=0;int result=-1;
  for(;list[count].if_index || list[count].if_name;count++) {
    if(count==DLE_INTERFACE_LIMIT || !list[count].if_index || !list[count].if_name ||
       strnlen(list[count].if_name,DLE_INTERFACE_NAME)==DLE_INTERFACE_NAME)
      {errno=EOVERFLOW;goto done;}
    current[count].index=list[count].if_index;
    memcpy(current[count].name,list[count].if_name,strlen(list[count].if_name)+1);
  }
  for(size_t i=1;i<count;i++) {
    struct dle_interface_identity value=current[i];size_t j=i;
    while(j && current[j-1].index>value.index) {current[j]=current[j-1];j--;}
    current[j]=value;
  }
  for(size_t i=0;i<count;i++) for(size_t j=0;j<i;j++)
    if(current[i].index==current[j].index || !strcmp(current[i].name,current[j].name))
      {errno=EPROTO;goto done;}
  if(capture) {
    memcpy(s->_baseline,current,count*sizeof(current[0]));s->_baseline_count=count;result=0;
  } else {
    result=count!=s->_baseline_count || memcmp(s->_baseline,current,count*sizeof(current[0]));
    result=result?1:0;
  }
done:
  {int saved=errno;if_freenameindex(list);errno=saved;}
  return result;
}
static int query(void *ctx,uint32_t dst,struct dle_route_view *out) {
  struct dle_interfaces *s=ctx;
  return dle_route_query(dst,out,s->_record,s->_context);
}
static int close_fd(void *ctx,int fd) {(void)ctx;return close(fd);}
static int cloexec(int fd) {
  int flags=fcntl(fd,F_GETFD);
  return flags<0?-1:fcntl(fd,F_SETFD,flags|FD_CLOEXEC);
}
static int finish_control(struct dle_interfaces *s,int fd,int result) {
  int saved=errno;
  if(close(fd)) {s->_descriptor_uncertain=1;if(!result)saved=errno;result=-1;}
  errno=saved;return result;
}
static int create(void *ctx,struct dle_interfaces *s) {
  (void)ctx;
  s->_owned_fd=socket(PF_SYSTEM,SOCK_DGRAM,SYSPROTO_CONTROL);
  if(s->_owned_fd<0) return -1;
  if(cloexec(s->_owned_fd)) return -1;
  struct ctl_info info;memset(&info,0,sizeof(info));
  memcpy(info.ctl_name,UTUN_CONTROL_NAME,sizeof(UTUN_CONTROL_NAME));
  if(ioctl(s->_owned_fd,CTLIOCGINFO,&info)) return -1;
  struct sockaddr_ctl control;memset(&control,0,sizeof(control));
  control.sc_len=sizeof(control);control.sc_family=AF_SYSTEM;
  control.ss_sysaddr=AF_SYS_CONTROL;control.sc_id=info.ctl_id; /* unit0 allocates fresh */
  if(connect(s->_owned_fd,(struct sockaddr *)&control,sizeof(control))) return -1;
  socklen_t n=sizeof(s->name);
  memset(s->name,0,sizeof(s->name));
  if(getsockopt(s->_owned_fd,SYSPROTO_CONTROL,UTUN_OPT_IFNAME,s->name,&n)) return -1;
  if(!n || n>sizeof(s->name) || !memchr(s->name,0,n) || !identity_name(s->name))
    {errno=EPROTO;return -1;}
  s->index=if_nametoindex(s->name);
  if(!s->index || s->index>UINT16_MAX) {errno=ENXIO;return -1;}
  for(size_t i=0;i<s->_baseline_count;i++)
    if(s->_baseline[i].index==s->index || !strcmp(s->_baseline[i].name,s->name))
      {errno=EEXIST;return -1;}
  s->_identity_known=1;
  int flags=fcntl(s->_owned_fd,F_GETFL);
  return flags<0?-1:fcntl(s->_owned_fd,F_SETFL,flags|O_NONBLOCK);
}
static struct sockaddr_in address(uint32_t ip) {
  struct sockaddr_in a;memset(&a,0,sizeof(a));
  a.sin_len=sizeof(a);a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(ip);return a;
}
static void request(struct ifreq *r,const struct dle_interfaces *s) {
  memset(r,0,sizeof(*r));memcpy(r->ifr_name,s->name,strlen(s->name)+1);
}
static int configure(void *ctx,struct dle_interfaces *s) {
  (void)ctx;
  int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
  int result=-1;
  if(cloexec(fd)) goto done;
  struct ifreq r;request(&r,s);r.ifr_mtu=1500;
  if(ioctl(fd,SIOCSIFMTU,&r)) goto done;
  struct ifaliasreq a;memset(&a,0,sizeof(a));
  memcpy(a.ifra_name,s->name,strlen(s->name)+1);
  struct sockaddr_in local=address(DLE_LOCAL_IPV4),peer=address(s->peer),mask=address(UINT32_MAX);
  memcpy(&a.ifra_addr,&local,sizeof(local));memcpy(&a.ifra_broadaddr,&peer,sizeof(peer));
  memcpy(&a.ifra_mask,&mask,sizeof(mask));
  if(ioctl(fd,SIOCAIFADDR,&a)) goto done;
  result=0;
done:return finish_control(s,fd,result);
}
static int check_address(int fd,const struct dle_interfaces *s,unsigned long command,uint32_t expected) {
  struct ifreq r;request(&r,s);
  if(ioctl(fd,command,&r)) return -1;
  struct sockaddr_in a;memcpy(&a,&r.ifr_addr,sizeof(a));
  /* Kernel netmask sin_len may be compressed by in_socktrim. */
  if(a.sin_family!=AF_INET || a.sin_port || ntohl(a.sin_addr.s_addr)!=expected ||
     (command==SIOCGIFNETMASK ? (a.sin_len<8 || a.sin_len>16) : a.sin_len!=16))
    {errno=EPROTO;return -1;}
  return 0;
}
static int inspect(void *ctx,struct dle_interfaces *s) {
  (void)ctx;
  if(if_nametoindex(s->name)!=s->index) {errno=ENXIO;return -1;}
  int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
  int result=-1;struct ifreq r;
  if(cloexec(fd))goto done;
  request(&r,s);
  if(ioctl(fd,SIOCGIFFLAGS,&r))goto done;
  if((r.ifr_flags&(IFF_UP|IFF_POINTOPOINT|IFF_RUNNING))!=(IFF_UP|IFF_POINTOPOINT|IFF_RUNNING) ||
     (r.ifr_flags&(IFF_BROADCAST|IFF_LOOPBACK))) {errno=EPROTO;goto done;}
  request(&r,s);
  if(ioctl(fd,SIOCGIFMTU,&r))goto done;
  if(r.ifr_mtu!=1500) {errno=EPROTO;goto done;}
  if(check_address(fd,s,SIOCGIFADDR,DLE_LOCAL_IPV4) ||
     check_address(fd,s,SIOCGIFDSTADDR,s->peer) || check_address(fd,s,SIOCGIFNETMASK,UINT32_MAX))goto done;
  result=0;
done:return finish_control(s,fd,result);
}
static uint64_t now(void *ctx) {
  (void)ctx;struct timespec t;
  if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
  return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static int pause_ms(void *ctx,unsigned ms) {
  (void)ctx;struct timespec t={(time_t)(ms/1000),(long)((ms%1000)*1000000)};
  /* EINTR returns to the outer bounded loop; never restart an unbounded sleep. */
  if(nanosleep(&t,NULL) && errno!=EINTR)return -1;
  return 0;
}
static const struct dle_interface_ops ops={inventory,query,create,configure,inspect,close_fd,now,pause_ms};
int dle_interfaces_open(struct dle_interfaces *s,uint32_t peer) {
  return dle_interfaces_open_core(s,peer,&ops,s);
}
int dle_interfaces_close(struct dle_interfaces *s) {
  return dle_interfaces_close_core(s,&ops,s);
}
