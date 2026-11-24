#include "coordinator.h"
#include <limits.h>
#include <string.h>
static uint64_t deadline(const struct dnds_state *s,uint64_t now,uint64_t span) {
 return span>s->whole_deadline-now?s->whole_deadline:now+span;
}
static uint32_t enter(struct dnds_state *s,enum dnds_phase phase,uint64_t now,
 uint64_t span,uint32_t action) {
 s->phase=phase;s->phase_deadline=deadline(s,now,span);return action;
}
static uint32_t link_stop(struct dnds_state *s,uint64_t now) {
 return enter(s,DNDS_LINK_DRAIN,now,DNDS_LINK_DRAIN_MS,DNDS_CLOSE_LINK);
}
static uint32_t daemon_stop(struct dnds_state *s,uint64_t now) {
 if(!s->daemon_pid||s->daemon_reaped)return link_stop(s,now);
 return enter(s,DNDS_DAEMON_DRAIN,now,DNDS_DAEMON_DRAIN_MS,DNDS_STOP_DAEMON);
}
static uint32_t frontend_stop(struct dnds_state *s,uint64_t now) {
 if(!s->frontend_pid||s->frontend_reaped)return daemon_stop(s,now);
 return enter(s,DNDS_FRONTEND_DRAIN,now,DNDS_FRONTEND_DRAIN_MS,DNDS_STOP_FRONTEND);
}
static uint32_t failed(struct dnds_state *s,uint64_t now,uint32_t error) {
 s->errors|=error;
 if(s->phase<DNDS_FRONTEND_DRAIN)return frontend_stop(s,now);
 return 0;
}
int dnds_init(struct dnds_state *s,uint64_t started) {
 if(!s||started>UINT64_MAX-DNDS_WHOLE_MS)return -1;
 memset(s,0,sizeof(*s));s->started=s->last=started;
 s->whole_deadline=started+DNDS_WHOLE_MS;
 s->phase_deadline=started+DNDS_SETUP_MS;s->phase=DNDS_PREFLIGHT;
 s->daemon_wait_status=s->frontend_wait_status=-1;return 0;
}
uint32_t dnds_step(struct dnds_state *s,uint64_t now,const struct dnds_event *e) {
 if(!s||!e)return 0;
 if(s->phase==DNDS_SEALED)return 0;
 if(now<s->last)return failed(s,s->last,DNDS_CLOCK_ERROR);
 s->last=now;
 if(now>=s->whole_deadline){
  s->errors|=DNDS_WHOLE_TIMEOUT;s->phase=DNDS_SEALED;s->complete=0;
  /* Failure requests remain explicit; adapter must retain all unreaped PIDs.
   * This is not an assertion that asynchronous retirement finished on time. */
  return DNDS_STOP_FRONTEND|DNDS_STOP_DAEMON|DNDS_CLOSE_LINK|DNDS_CLOSE_ROUTE|DNDS_SEAL;
 }
 /* Actual owner adoption must happen before this call. Keep a created identity
  * even if its spawn-local cleanup failed; never reinterpret it as no child. */
 if(e->kind==DNDS_DAEMON_CREATED||e->kind==DNDS_FRONTEND_CREATED){
  int daemon=e->kind==DNDS_DAEMON_CREATED;
  int32_t *target=daemon?&s->daemon_pid:&s->frontend_pid;
  int32_t other=daemon?s->frontend_pid:s->daemon_pid;
  if(e->pid<2||*target||other==e->pid)return failed(s,now,DNDS_BAD_EVENT);
  *target=e->pid;
  if(e->local_spawn_error)return failed(s,now,DNDS_SPAWN_ERROR);
  if(now>=s->phase_deadline)return failed(s,now,DNDS_SETUP_TIMEOUT)|(daemon?DNDS_STOP_DAEMON:DNDS_STOP_FRONTEND);
  if(s->phase!=(daemon?DNDS_DAEMON_WAIT:DNDS_FRONTEND_WAIT))return failed(s,now,DNDS_BAD_EVENT);
  if(!daemon)return enter(s,DNDS_ACTIVE,now,DNDS_DESKTOP_MS,0);
  return 0;
 }
 /* Record an actual owned reap even when it violates successful phase order.
  * The real child_owner remains authoritative and forbids cached-PID signals. */
 int new_reap=0;
 if(e->kind==DNDS_FRONTEND_CLOSED&&e->pid==s->frontend_pid&&s->frontend_pid&&!s->frontend_reaped){
  s->frontend_reaped=1;s->frontend_wait_status=e->wait_status;new_reap=1;
 }
 if(e->kind==DNDS_DAEMON_CLOSED&&e->pid==s->daemon_pid&&s->daemon_pid&&!s->daemon_reaped){
  s->daemon_reaped=1;s->daemon_wait_status=e->wait_status;new_reap=1;
 }
 if(e->kind==DNDS_FAILURE)return failed(s,now,DNDS_EXTERNAL_FAILURE);
 /* A proof arriving at/after its existing deadline cannot refresh that phase.
  * Explicit closed/reaped events below still retire a correctly owned identity. */
 if(now>=s->phase_deadline){
  if(s->phase<DNDS_ACTIVE)return failed(s,now,DNDS_SETUP_TIMEOUT);
  if(s->phase==DNDS_ACTIVE){
   uint32_t action=frontend_stop(s,now);
   if(e->kind==DNDS_TICK)return action;
   /* Don't lose the one stop request when consuming a simultaneous reap. */
   if(e->kind!=DNDS_FRONTEND_CLOSED)return action|failed(s,now,DNDS_BAD_EVENT);
   if(e->pid!=s->frontend_pid||!new_reap)return action|failed(s,now,DNDS_BAD_EVENT);
   s->frontend_reaped=1;s->frontend_wait_status=e->wait_status;
   if(e->wait_status||e->proof!=DNDS_FRONTEND_PROOF)s->errors|=DNDS_BAD_PROOF;
   return action|daemon_stop(s,now);
  }
  s->errors|=DNDS_DRAIN_TIMEOUT;
  if(s->phase==DNDS_FRONTEND_DRAIN&&e->kind!=DNDS_FRONTEND_CLOSED)return daemon_stop(s,now);
  if(s->phase==DNDS_DAEMON_DRAIN&&e->kind!=DNDS_DAEMON_CLOSED)return link_stop(s,now);
  if(s->phase==DNDS_LINK_DRAIN&&e->kind!=DNDS_LINK_CLOSED)return enter(s,DNDS_ROUTE_DRAIN,now,s->whole_deadline-now,DNDS_CLOSE_ROUTE);
 }
 switch(e->kind){
 case DNDS_TICK:return 0;
 case DNDS_PREFLIGHT_OK:
  if(s->phase!=DNDS_PREFLIGHT||e->proof!=DNDS_PREFLIGHT_PROOF)return failed(s,now,DNDS_BAD_EVENT);
  s->phase=DNDS_ROUTE_WAIT;return DNDS_OPEN_ROUTE;
 case DNDS_ROUTE_READY:
  if(s->phase!=DNDS_ROUTE_WAIT||e->proof!=DNDS_ROUTE_PROOF)return failed(s,now,DNDS_BAD_PROOF);
  s->phase=DNDS_DAEMON_WAIT;return DNDS_START_DAEMON;
 case DNDS_DAEMON_READY:
  if(s->phase!=DNDS_DAEMON_WAIT||!s->daemon_pid||e->pid!=s->daemon_pid||e->proof!=DNDS_DAEMON_READY_PROOF)return failed(s,now,DNDS_BAD_PROOF);
  s->phase=DNDS_FRONTEND_WAIT;return DNDS_START_FRONTEND;
 case DNDS_FRONTEND_CLOSED:
  if((s->phase!=DNDS_ACTIVE&&s->phase!=DNDS_FRONTEND_DRAIN)||!new_reap)return failed(s,now,DNDS_BAD_EVENT);
  s->frontend_reaped=1;s->frontend_wait_status=e->wait_status;
  if(e->wait_status||e->proof!=DNDS_FRONTEND_PROOF)s->errors|=DNDS_BAD_PROOF;
  return daemon_stop(s,now);
 case DNDS_DAEMON_CLOSED:
  if(s->phase!=DNDS_DAEMON_DRAIN||!new_reap)return failed(s,now,DNDS_BAD_EVENT);
  s->daemon_reaped=1;s->daemon_wait_status=e->wait_status;
  if(e->wait_status!=1024||e->proof!=DNDS_DAEMON_CLOSED_PROOF)s->errors|=DNDS_BAD_PROOF;
  return link_stop(s,now);
 case DNDS_LINK_CLOSED:
  if(s->phase!=DNDS_LINK_DRAIN)return failed(s,now,DNDS_BAD_EVENT);
  if(e->proof!=DNDS_LINK_PROOF)s->errors|=DNDS_BAD_PROOF;
  return enter(s,DNDS_ROUTE_DRAIN,now,s->whole_deadline-now,DNDS_CLOSE_ROUTE);
 case DNDS_ROUTE_CLOSED:
  if(s->phase!=DNDS_ROUTE_DRAIN)return failed(s,now,DNDS_BAD_EVENT);
  if(e->proof!=DNDS_ROUTE_CLOSED_PROOF)s->errors|=DNDS_BAD_PROOF;
  s->phase=DNDS_SEALED;s->complete=!s->errors&&s->frontend_reaped&&s->daemon_reaped;
  return DNDS_SEAL;
 default:return failed(s,now,DNDS_BAD_EVENT);
 }
}
