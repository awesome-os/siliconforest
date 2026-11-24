"""Real source transition bodies with deterministic resource/proof adapters.
This tests call order and refusal branches, not a native service or record proof.
"""
from pathlib import Path
import json,subprocess,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
source=(HERE/'desktop_network.c').read_text()
body=source[source.index('static int service_start('):source.index('static int can_transition(const struct state *s) {')]
before=r'''
#ifdef NDEBUG
#error assertions required
#endif
#include "owned_child.h"
#include "endpoint_owner.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
enum phase {WAIT_OPEN,SERVICE_START,DESKTOP_ACTIVE,SERVICE_STOP,CLOSE_WAIT};
struct state {struct dnds_child daemon,frontend;struct dlr_endpoints endpoints;int paths,desktop,desktop_ok,daemon_cleanup,frontend_recorded;char nonce[49];const char *error;enum phase phase;uint64_t deadline,setup_deadline,service_deadline,frontend_deadline,stop_deadline;int partial;};
static int preflight_bad,ready_value,start_bad,cleanup_bad,path_bad,proof_bad,observation_bad,proof_observation_bad;
static int starts,cleanups,proofs,observations,stops;static uint64_t clock_value,stopped_at;
static int fail(struct state *s,const char *e){if(!s->error)s->error=e;return -1;}
static uint64_t ms(void){return clock_value;}
static int can_transition(const struct state *s){return !s->partial;}
static void observe(const char *k,const uint8_t *b,size_t n,void *c){(void)k;(void)b;(void)n;(void)c;}
static void child_observation(struct state *s,const char *k,const struct dnds_child *c){assert(!strcmp(k,"frontend_child")&&c==&s->frontend);observations++;if(observation_bad)fail(s,"observation");}
int dlr_endpoints_preflight(struct dlr_endpoints *e){e->initialized=1;return preflight_bad?-1:0;}
int dlr_endpoints_ready(struct dlr_endpoints *e,int32_t pid){(void)e;assert(pid==77);return ready_value;}
int dlr_endpoints_cleanup(struct dlr_endpoints *e,int reaped){assert(reaped);cleanups++;if(cleanup_bad)return -1;e->cleaned=1;return 0;}
int dnds_child_start(struct dnds_child *c,enum dnds_child_kind k,const char *n,uint64_t now){assert(strlen(n)==48&&now);c->initialized=c->created=1;c->kind=k;c->pid=k==DNDS_MDNS?77:78;c->started=now;starts++;return start_bad?-1:0;}
void dnds_child_tick(struct dnds_child *c,uint64_t n){(void)c;(void)n;}
void dnds_child_stop(struct dnds_child *c,uint64_t n){assert(c->kind==DNDS_MDNS);c->stop_requested=c->term_sent=1;stops++;stopped_at=n;}
int dnds_child_done(const struct dnds_child *c){return c->created&&c->reaped&&!c->lost&&c->output_fd<0&&c->error_fd<0;}
int dnds_paths_verify(void *p){(void)p;return path_bad?-1:0;}
int dnds_verify_closed(const char *nonce,int32_t pid,void(*o)(const char*,const uint8_t*,size_t,void*),void *c,void *facts){(void)facts;assert(strlen(nonce)==48&&pid==78&&o==observe);proofs++;if(proof_observation_bad)fail(c,"proof_observation");return proof_bad?-1:0;}
'''
after=r'''
static struct state fresh(void){struct state s;memset(&s,0,sizeof(s));memcpy(s.nonce,"0123456789abcdef0123456789abcdef0123456789abcdef",49);s.deadline=180000;s.setup_deadline=20000;preflight_bad=start_bad=cleanup_bad=path_bad=proof_bad=observation_bad=proof_observation_bad=starts=cleanups=proofs=observations=stops=0;ready_value=0;clock_value=1000;stopped_at=0;return s;}
static void frontend_ready(struct state *s){assert(!service_start(s,100));ready_value=1;assert(!service_tick(s,101)&&s->phase==DESKTOP_ACTIVE&&starts==2);s->frontend.reaped=s->frontend.output_eof=s->frontend.error_eof=1;s->frontend.output_fd=s->frontend.error_fd=-1;}
static void daemon_stopped(struct state *s){s->phase=SERVICE_STOP;s->stop_deadline=6000;s->daemon.reaped=s->daemon.term_sent=s->daemon.output_eof=s->daemon.error_eof=1;s->daemon.output_fd=s->daemon.error_fd=-1;s->daemon.wait_status=EINTR<<8;}
int main(void){
 assert(ms()==clock_value);
 struct state s=fresh();frontend_ready(&s);assert(!service_tick(&s,102)&&s.desktop_ok&&s.frontend_recorded&&proofs==1&&observations==1&&stops==1&&stopped_at==1000&&s.phase==SERVICE_STOP&&s.stop_deadline==6000);
 daemon_stopped(&s);assert(!service_tick(&s,1001)&&s.daemon_cleanup&&cleanups==1);assert(!service_tick(&s,1002)&&cleanups==1);s.phase=CLOSE_WAIT;assert(!service_tick(&s,1003));
 s=fresh();preflight_bad=1;assert(service_start(&s,100)<0&&!starts);
 s=fresh();start_bad=1;assert(service_start(&s,100)<0&&s.daemon.created);
 s=fresh();assert(service_start(&s,20000)<0&&!starts);
 s=fresh();assert(!service_start(&s,100));assert(!service_tick(&s,101)&&starts==1);assert(service_tick(&s,5100)<0&&!s.frontend.created);
 s=fresh();assert(!service_start(&s,100));ready_value=1;s.partial=1;assert(!service_tick(&s,101)&&starts==1&&!s.frontend.created);s.partial=0;assert(!service_tick(&s,102)&&starts==2);
 s=fresh();assert(!service_start(&s,100));s.daemon.reaped=1;assert(service_tick(&s,101)<0&&starts==1);
 for(unsigned f=0;f<11;f++){
  s=fresh();frontend_ready(&s);
  if(f==0)s.frontend.wait_status=9;else if(f==1)s.frontend.wait_status=1<<8;
  else if(f==2)s.frontend.kill_sent=1;else if(f==3)s.frontend.output_eof=0;
  else if(f==4)s.frontend.error_eof=0;else if(f==5)observation_bad=1;
  else if(f==6)path_bad=1;else if(f==7)proof_bad=1;
  else if(f==8)proof_observation_bad=1;else if(f==9)clock_value=0;else clock_value=s.deadline;
  assert(service_tick(&s,102)<0&&!stops&&!s.daemon_cleanup);
 }
 for(unsigned f=0;f<7;f++){
  s=fresh();assert(!service_start(&s,100));daemon_stopped(&s);
  if(f==0)s.daemon.wait_status=0;else if(f==1)s.daemon.wait_status=15;
  else if(f==2)s.daemon.kill_sent=1;else if(f==3)s.daemon.term_sent=0;
  else if(f==4)s.daemon.output_eof=0;else if(f==5)s.daemon.error_eof=0;else cleanup_bad=1;
  assert(service_tick(&s,102)<0&&!s.daemon_cleanup);
 }
 s=fresh();assert(!service_start(&s,100));s.phase=SERVICE_STOP;s.stop_deadline=1000;assert(service_tick(&s,1000)<0&&!strcmp(s.error,"mdns_reap_deadline"));
 s=fresh();frontend_ready(&s);s.frontend.reaped=0;assert(service_tick(&s,s.frontend_deadline)<0&&!s.desktop_ok);
 s=fresh();frontend_ready(&s);s.frontend.error=5;assert(service_tick(&s,102)<0&&!proofs);
 s=fresh();frontend_ready(&s);s.daemon.error=5;assert(service_tick(&s,102)<0&&!proofs);
 puts("29 actual service branch groups passed");return 0;
}
'''
def main():
    with tempfile.TemporaryDirectory(prefix='dnds-transitions-') as tmp:
        tmp=Path(tmp)
        def run(text,name):
            src=tmp/(name+'.c');exe=tmp/name;src.write_text(before+text+after)
            cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-fsanitize=address,undefined',
                 '-I'+str(HERE),'-I'+str(ROOT/'integrations/puredarwin/m1-network-resolver-service/guest'),
                 '-I'+str(ROOT/'integrations/puredarwin/chonkstep-darwin/darwin-spawn'),str(src),'-o',str(exe)]
            p=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            if p.returncode:raise RuntimeError(p.stderr)
            return subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
        p=run(body,'baseline')
        if p.returncode:raise RuntimeError(p.stdout+p.stderr)
        mutations={
            'ignore-proof-error':('if(dnds_verify_closed(s->nonce,s->frontend.pid,observe,s,&s->desktop))','if((dnds_verify_closed(s->nonce,s->frontend.pid,observe,s,&s->desktop),0))'),
            'ignore-path-identity':('if(dnds_paths_verify(&s->paths))','if((dnds_paths_verify(&s->paths),0))'),
            'stale-post-proof-clock':('uint64_t fresh=ms();','uint64_t fresh=now;'),
            'ignore-frontend-signal':('!WIFEXITED(s->frontend.wait_status)||WEXITSTATUS(s->frontend.wait_status)!=0||','0||'),
            'ignore-frontend-kill':('s->frontend.kill_sent||','0||'),
            'missing-frontend-output-EOF':('!s->frontend.output_eof||','0||'),
            'ignore-daemon-exit':('!WIFEXITED(s->daemon.wait_status)||WEXITSTATUS(s->daemon.wait_status)!=EINTR||','0||'),
            'ignore-observation-failure':('if(s->error)return -1;','if(0)return -1;'),
            'ignore-setup-budget':('if(now>=s->setup_deadline)','if(0)'),
        }
        killed=[]
        for name,(a,b) in mutations.items():
            assert a in body,name
            p=run(body.replace(a,b),name)
            if p.returncode==0 or 'Assertion' not in p.stderr:raise RuntimeError('survived '+name+' '+p.stderr)
            killed.append(name)
        print(json.dumps({'status':'PASS','actual_branch_groups':29,'compiled_mutants_killed':killed}))
if __name__=='__main__':main()
