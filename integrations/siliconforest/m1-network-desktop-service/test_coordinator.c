/* Real execution of pure C model; all runtime observations below are synthetic. */
#include "coordinator.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"check failed line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t step(struct dnds_state *s,uint64_t time,enum dnds_kind kind,
 int32_t pid,int32_t wait,uint32_t proof,uint32_t error) {
 struct dnds_event e={kind,pid,wait,proof,error};return dnds_step(s,time,&e);
}
static struct dnds_state active(void) {
 struct dnds_state s;CHECK(!dnds_init(&s,100));
 CHECK(step(&s,101,DNDS_PREFLIGHT_OK,0,0,DNDS_PREFLIGHT_PROOF,0)==DNDS_OPEN_ROUTE);
 CHECK(step(&s,8100,DNDS_ROUTE_READY,0,0,DNDS_ROUTE_PROOF,0)==DNDS_START_DAEMON);
 CHECK(!step(&s,8200,DNDS_DAEMON_CREATED,9,0,0,0));
 CHECK(step(&s,10000,DNDS_DAEMON_READY,9,0,DNDS_DAEMON_READY_PROOF,0)==DNDS_START_FRONTEND);
 CHECK(!step(&s,11000,DNDS_FRONTEND_CREATED,10,0,0,0));
 CHECK(s.phase==DNDS_ACTIVE&&s.phase_deadline==131000);
 return s;
}
static void finish(struct dnds_state *s,uint64_t now,uint32_t front,uint32_t daemon,
 uint32_t link,uint32_t route,int fw,int dw) {
 CHECK(step(s,now,DNDS_FRONTEND_CLOSED,10,fw,front,0)==DNDS_STOP_DAEMON);
 CHECK(step(s,now+1,DNDS_DAEMON_CLOSED,9,dw,daemon,0)==DNDS_CLOSE_LINK);
 CHECK(step(s,now+2,DNDS_LINK_CLOSED,0,0,link,0)==DNDS_CLOSE_ROUTE);
 CHECK(step(s,now+3,DNDS_ROUTE_CLOSED,0,0,route,0)==DNDS_SEAL);
 CHECK(s->phase==DNDS_SEALED&&s->frontend_reaped&&s->daemon_reaped);
}
static void success(void) {
 struct dnds_state s=active();
 CHECK(step(&s,131000,DNDS_TICK,0,0,0,0)==DNDS_STOP_FRONTEND);
 CHECK(s.phase==DNDS_FRONTEND_DRAIN&&s.phase_deadline==141000&&!s.errors);
 for(uint64_t t=131001;t<141000;t+=1000){
  CHECK(!step(&s,t,DNDS_TICK,0,0,0,0));CHECK(s.phase_deadline==141000);
 }
 finish(&s,140999,DNDS_FRONTEND_PROOF,DNDS_DAEMON_CLOSED_PROOF,DNDS_LINK_PROOF,DNDS_ROUTE_CLOSED_PROOF,0,1024);
 CHECK(s.complete&&!s.errors);
 CHECK(!step(&s,180100,DNDS_TICK,0,0,0,0));
 struct dnds_state early=active();finish(&early,12000,DNDS_FRONTEND_PROOF,DNDS_DAEMON_CLOSED_PROOF,DNDS_LINK_PROOF,DNDS_ROUTE_CLOSED_PROOF,0,1024);CHECK(early.complete);
}
static void every_proof_bit_and_unknown_bit(void) {
 for(unsigned i=0;i<16;i++){
  struct dnds_state s=active();finish(&s,12000,DNDS_FRONTEND_PROOF^(1u<<i),DNDS_DAEMON_CLOSED_PROOF,DNDS_LINK_PROOF,DNDS_ROUTE_CLOSED_PROOF,0,1024);CHECK(!s.complete&&(s.errors&DNDS_BAD_PROOF));
 }
 for(unsigned stage=0;stage<3;stage++){
  unsigned count=stage==0?6:stage==1?4:3;
  for(unsigned i=0;i<=count;i++){
   struct dnds_state s=active();uint32_t dp=DNDS_DAEMON_CLOSED_PROOF,lp=DNDS_LINK_PROOF,rp=DNDS_ROUTE_CLOSED_PROOF;
   if(stage==0)dp^=1u<<i;else if(stage==1)lp^=1u<<i;else rp^=1u<<i;
   finish(&s,12000,DNDS_FRONTEND_PROOF,dp,lp,rp,0,1024);CHECK(!s.complete&&(s.errors&DNDS_BAD_PROOF));
  }
 }
 struct dnds_state extra=active();finish(&extra,12000,DNDS_FRONTEND_PROOF|0x10000,DNDS_DAEMON_CLOSED_PROOF,DNDS_LINK_PROOF,DNDS_ROUTE_CLOSED_PROOF,0,1024);CHECK(!extra.complete);
 for(int w=1;w<=15;w+=14){struct dnds_state s=active();finish(&s,12000,DNDS_FRONTEND_PROOF,DNDS_DAEMON_CLOSED_PROOF,DNDS_LINK_PROOF,DNDS_ROUTE_CLOSED_PROOF,w,1024);CHECK(!s.complete);}
 for(int w=0;w<=15;w+=15){struct dnds_state s=active();finish(&s,12000,DNDS_FRONTEND_PROOF,DNDS_DAEMON_CLOSED_PROOF,DNDS_LINK_PROOF,DNDS_ROUTE_CLOSED_PROOF,0,w);CHECK(!s.complete);}
}
static void failure_and_timeouts(void) {
 struct dnds_state s=active();
 CHECK(step(&s,12000,DNDS_FAILURE,0,0,0,0)==DNDS_STOP_FRONTEND);
 CHECK(s.errors&DNDS_EXTERNAL_FAILURE);
 finish(&s,12001,DNDS_FRONTEND_PROOF,DNDS_DAEMON_CLOSED_PROOF,DNDS_LINK_PROOF,DNDS_ROUTE_CLOSED_PROOF,0,1024);CHECK(!s.complete);
 s=active();CHECK(step(&s,131000,DNDS_TICK,0,0,0,0)==DNDS_STOP_FRONTEND);
 CHECK(step(&s,141000,DNDS_TICK,0,0,0,0)==DNDS_STOP_DAEMON);
 CHECK(!s.frontend_reaped&&s.frontend_pid==10);
 CHECK(step(&s,146000,DNDS_TICK,0,0,0,0)==DNDS_CLOSE_LINK);
 CHECK(!s.daemon_reaped&&s.daemon_pid==9);
 CHECK(step(&s,161000,DNDS_TICK,0,0,0,0)==DNDS_CLOSE_ROUTE);
 CHECK(step(&s,161001,DNDS_ROUTE_CLOSED,0,0,DNDS_ROUTE_CLOSED_PROOF,0)==DNDS_SEAL);
 CHECK(!s.complete&&s.errors&&s.frontend_pid==10&&s.daemon_pid==9);
 s=active();uint32_t all=DNDS_STOP_FRONTEND|DNDS_STOP_DAEMON|DNDS_CLOSE_LINK|DNDS_CLOSE_ROUTE|DNDS_SEAL;
 CHECK(step(&s,180100,DNDS_TICK,0,0,0,0)==all);CHECK(!s.complete&&s.errors&DNDS_WHOLE_TIMEOUT);
 CHECK(s.frontend_pid==10&&s.daemon_pid==9&&!s.frontend_reaped&&!s.daemon_reaped);
 s=active();CHECK(step(&s,10999,DNDS_TICK,0,0,0,0)==DNDS_STOP_FRONTEND);CHECK(s.errors&DNDS_CLOCK_ERROR);
}
static void adoption_and_phase_order(void) {
 struct dnds_state s;CHECK(!dnds_init(&s,0));
 CHECK(step(&s,1,DNDS_PREFLIGHT_OK,0,0,DNDS_PREFLIGHT_PROOF,0)==DNDS_OPEN_ROUTE);
 CHECK(step(&s,2,DNDS_ROUTE_READY,0,0,DNDS_ROUTE_PROOF,0)==DNDS_START_DAEMON);
 CHECK(step(&s,3,DNDS_DAEMON_CREATED,9,0,0,5)==DNDS_STOP_DAEMON);CHECK(s.daemon_pid==9&&s.errors&DNDS_SPAWN_ERROR);
 s=active();CHECK(step(&s,12000,DNDS_FRONTEND_CREATED,11,0,0,0)==DNDS_STOP_FRONTEND);CHECK(s.frontend_pid==10&&s.errors&DNDS_BAD_EVENT); /* adapter retains rejected PID11 */
 s=active();CHECK(step(&s,12000,DNDS_DAEMON_CLOSED,9,1024,DNDS_DAEMON_CLOSED_PROOF,0)==DNDS_STOP_FRONTEND);CHECK(s.daemon_reaped&&s.errors);
 CHECK(step(&s,12001,DNDS_FRONTEND_CLOSED,10,0,DNDS_FRONTEND_PROOF,0)==DNDS_CLOSE_LINK); /* no signal request to reaped daemon */
 s=active();CHECK(step(&s,12000,DNDS_FRONTEND_CLOSED,99,0,DNDS_FRONTEND_PROOF,0)==DNDS_STOP_FRONTEND);CHECK(!s.frontend_reaped&&s.frontend_pid==10);
 s=active();CHECK(step(&s,12000,DNDS_LINK_CLOSED,0,0,DNDS_LINK_PROOF,0)==DNDS_STOP_FRONTEND);CHECK(s.errors);
 s=active();CHECK(step(&s,12000,DNDS_DAEMON_CREATED,10,0,0,0)==DNDS_STOP_FRONTEND);CHECK(s.daemon_pid==9);
 s=active();CHECK(step(&s,131000,DNDS_FRONTEND_CLOSED,10,0,DNDS_FRONTEND_PROOF,0)==DNDS_STOP_DAEMON);CHECK(s.frontend_reaped&&!s.errors); /* exact simultaneous boundary */
}
static void setup_deadline_and_limits(void) {
 for(int event=DNDS_TICK;event<=DNDS_ROUTE_CLOSED;event++){
  struct dnds_state s;CHECK(!dnds_init(&s,0));
  if(event==DNDS_DAEMON_CREATED){CHECK(step(&s,1,DNDS_PREFLIGHT_OK,0,0,DNDS_PREFLIGHT_PROOF,0)==DNDS_OPEN_ROUTE);CHECK(step(&s,2,DNDS_ROUTE_READY,0,0,DNDS_ROUTE_PROOF,0)==DNDS_START_DAEMON);}
  (void)step(&s,20000,(enum dnds_kind)event,9,0,0,0);CHECK(s.errors&&!s.complete);
  if(event==DNDS_DAEMON_CREATED)CHECK(s.daemon_pid==9);
 }
 struct dnds_state s;CHECK(dnds_init(&s,UINT64_MAX)<0);CHECK(dnds_init(NULL,0)<0);
 CHECK(!dnds_init(&s,UINT64_MAX-DNDS_WHOLE_MS));CHECK(s.whole_deadline==UINT64_MAX);
 CHECK(step(&s,UINT64_MAX,DNDS_TICK,0,0,0,0)&DNDS_SEAL);CHECK(!s.complete);
 for(unsigned mask=0;mask<DNDS_PREFLIGHT_PROOF;mask++){
  CHECK(!dnds_init(&s,0));(void)step(&s,1,DNDS_PREFLIGHT_OK,0,0,mask,0);CHECK(s.errors&&s.phase!=DNDS_ROUTE_WAIT);
 }
}
int main(void){success();every_proof_bit_and_unknown_bit();failure_and_timeouts();adoption_and_phase_order();setup_deadline_and_limits();printf("coordinator: %u pure-model assertions passed; synthetic inputs only\n",checks);return 0;}
