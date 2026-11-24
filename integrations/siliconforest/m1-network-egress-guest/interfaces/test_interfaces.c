#include "interface_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
struct fake {
  struct dle_interfaces *s;
  int calls, fail_at, creates, closes, live, configured, close_error;
  int inventory_changed, inventory_delay, residual_route, wrong_route, unknown_identity;
  uint64_t clock;
};
static unsigned cases;
static int fault(struct fake *f) {
  f->calls++;if(f->calls==f->fail_at){errno=EIO;return -1;}return 0;
}
static int inventory(void *p,struct dle_interfaces *s,int capture) {
  struct fake *f=p;if(fault(f))return -1;
  if(capture){s->_baseline_count=1;s->_baseline[0].index=1;strcpy(s->_baseline[0].name,"lo0");return 0;}
  if(f->inventory_delay>0){f->inventory_delay--;return 1;}
  return f->live || f->inventory_changed;
}
static int query(void *p,uint32_t dst,struct dle_route_view *v) {
  struct fake *f=p;if(fault(f))return -1;
  memset(v,0,sizeof(*v));v->have_destination=1;v->destination=dst;
  if(dst==f->s->peer && (f->configured || f->residual_route)) {
    v->have_gateway=1;v->gateway=DLE_LOCAL_IPV4;v->flags=0x45;v->interface_index=7;
    switch(f->wrong_route) {
      case 1:v->flags|=2;break;
      case 2:v->flags&=~4u;break;
      case 3:v->gateway++;break;
      case 4:v->interface_index++;break;
      case 5:v->destination++;break;
      case 6:v->have_netmask=1;v->netmask=0xffffff00;break;
      case 7:v->error=ESRCH;break;
      case 8:v->flags|=0x800;break;
    }
  } else v->error=ESRCH;
  return 0;
}
static int create(void *p,struct dle_interfaces *s) {
  struct fake *f=p;CHECK(s->fd==-1);f->creates++;s->_owned_fd=99;f->live=1;
  strcpy(s->name,"utun5");s->index=7;s->_identity_known=!f->unknown_identity;
  return fault(f);
}
static int configure(void *p,struct dle_interfaces *s) {
  struct fake *f=p;CHECK(s->fd==-1);f->configured=1;return fault(f);
}
static int inspect(void *p,struct dle_interfaces *s) {
  struct fake *f=p;CHECK(s->fd==-1);return fault(f);
}
static int close_fd(void *p,int fd) {
  struct fake *f=p;CHECK(fd==99);f->closes++;f->live=0;f->configured=0;
  if(f->close_error){errno=EIO;return -1;}return 0;
}
static uint64_t now(void *p){struct fake *f=p;return f->clock;}
static int pause_ms(void *p,unsigned ms){struct fake *f=p;f->clock+=ms;return 0;}
static const struct dle_interface_ops ops={inventory,query,create,configure,inspect,close_fd,now,pause_ms};
static void init(struct dle_interfaces *s,struct fake *f) {
  memset(f,0,sizeof(*f));f->s=s;f->clock=100;
  CHECK(dle_interfaces_init(s,1,NULL,NULL)==0);CHECK(s->fd==-1 && s->_owned_fd==-1 && s->cleanup_complete);
}
static int open_dns(struct dle_interfaces *s,struct fake *f){return dle_interfaces_open_core(s,DLE_DNS_IPV4,&ops,f);}
static int close_phase(struct dle_interfaces *s,struct fake *f){return dle_interfaces_close_core(s,&ops,f);}
int main(void) {
  struct dle_interfaces s;struct fake f;
  /* Expected rtinit direct-host GET: IPv4 gateway is local address, no mask. */
  uint8_t wire[124]={0};struct dle_route_view route;
  wire[0]=124;wire[2]=5;wire[3]=4;wire[4]=7;wire[8]=0x45;wire[12]=3;wire[16]=42;wire[20]=1;
  wire[92]=16;wire[93]=2;wire[96]=10;wire[98]=2;wire[99]=3;
  wire[108]=16;wire[109]=2;wire[112]=10;wire[114]=2;wire[115]=15;
  CHECK(dle_route_decode(wire,sizeof(wire),42,1,4,&route)==0);
  CHECK(route.destination==DLE_DNS_IPV4 && route.gateway==DLE_LOCAL_IPV4 &&
        route.flags==0x45 && route.interface_index==7 && !route.have_netmask);
  wire[109]=18;CHECK(dle_route_decode(wire,sizeof(wire),42,1,4,&route)==-1);cases++;
  init(&s,&f);CHECK(open_dns(&s,&f)==0);CHECK(s.fd==99 && s.phase_count==1 && !s.cleanup_complete);
  f.inventory_delay=3;CHECK(close_phase(&s,&f)==0);CHECK(s.fd==-1 && s.cleanup_complete && f.closes==1);
  CHECK(dle_interfaces_open_core(&s,0x5db8d822,&ops,&f)==0);CHECK(s.phase_count==2);
  CHECK(close_phase(&s,&f)==0);CHECK(s.cleanup_complete && !s.failed && f.closes==2);
  CHECK(close_phase(&s,&f)==0 && f.closes==2);cases++;
  CHECK(dle_interfaces_open_core(&s,0x5db8d822,&ops,&f)==-1);CHECK(f.creates==2);cases++;
  for(int at=1;at<=8;at++) {
    init(&s,&f);f.fail_at=at;CHECK(open_dns(&s,&f)==-1);CHECK(s.failed && s.fd==-1 && s._owned_fd==-1);
    CHECK(f.closes==f.creates);int old_calls=f.calls;
    CHECK(open_dns(&s,&f)==-1 && f.calls==old_calls);
    (void)close_phase(&s,&f);CHECK(f.closes==f.creates);cases++;
  }
  for(int bad=1;bad<=8;bad++) {
    init(&s,&f);f.wrong_route=bad;CHECK(open_dns(&s,&f)==-1);
    CHECK(s.failed && s.fd==-1 && f.creates==1 && f.closes==1);cases++;
  }
  init(&s,&f);f.unknown_identity=1;CHECK(open_dns(&s,&f)==-1 && f.closes==1);cases++;
  init(&s,&f);CHECK(open_dns(&s,&f)==0);f.close_error=1;
  CHECK(close_phase(&s,&f)==-1 && !s.cleanup_complete && s._descriptor_uncertain && s.failed);
  CHECK(close_phase(&s,&f)==-1 && f.closes==1);cases++;
  init(&s,&f);CHECK(open_dns(&s,&f)==0);f.inventory_changed=1;
  CHECK(close_phase(&s,&f)==-1 && !s.cleanup_complete && f.closes==1);cases++;
  init(&s,&f);CHECK(open_dns(&s,&f)==0);f.residual_route=1;
  CHECK(close_phase(&s,&f)==-1 && !s.cleanup_complete && f.closes==1);cases++;
  f.residual_route=0;CHECK(close_phase(&s,&f)==-1 && s.cleanup_complete && s.failed);cases++;
  init(&s,&f);CHECK(open_dns(&s,&f)==0);f.clock=0;
  CHECK(close_phase(&s,&f)==-1 && f.closes==1 && !s.cleanup_complete);cases++;
  init(&s,&f);f.residual_route=1;CHECK(open_dns(&s,&f)==-1 && !f.creates);cases++;
  init(&s,&f);CHECK(dle_interfaces_open_core(&s,0x08080808,&ops,&f)==-1 && !f.creates);cases++;
  init(&s,&f);CHECK(open_dns(&s,&f)==0);CHECK(close_phase(&s,&f)==0);
  f.inventory_changed=1;CHECK(dle_interfaces_open_core(&s,0x5db8d822,&ops,&f)==-1 && f.creates==1);cases++;
  const uint32_t bad_ips[]={0,0xffffffff,0x0a000203,0x7f000001,0xe0000001,0xc0000201};
  for(size_t i=0;i<sizeof(bad_ips)/sizeof(bad_ips[0]);i++) {
    init(&s,&f);CHECK(open_dns(&s,&f)==0);CHECK(close_phase(&s,&f)==0);
    CHECK(dle_interfaces_open_core(&s,bad_ips[i],&ops,&f)==-1 && f.creates==1);cases++;
  }
  CHECK(dle_interfaces_init(&s,0,NULL,NULL)==-1 && s.fd==-1 && s.failed);cases++;
  printf("PASS %u synthetic lifecycle/fault cases; no Darwin execution\n",cases);
  return 0;
}
