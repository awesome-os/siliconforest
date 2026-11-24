"""Compile the real service transition bodies against deterministic adapters.
Host state-machine evidence, not native resolver execution or a boot verdict.
"""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parent
s=(ROOT/'net_resolver.c').read_text()
a=s.index('static int service_start(');b=s.index('static int http_start(',a)
body=s[a:b]
harness=r'''
#include "child_owner.h"
#include "endpoint_owner.h"
#include "probe_record.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
enum phase { WAIT_OPEN,SERVICE_START,RESOLVING,DNS_DRAIN,HTTP_ACTIVE,HTTP_DRAIN,SERVICE_STOP,CLOSE_WAIT };
struct state { struct dlr_child daemon,probe;struct dlr_endpoints endpoints;struct dlr_probe_record resolver;char nonce[49];const char *error;int resolver_ok,dns_ok,daemon_cleanup;uint32_t selected;enum phase phase;uint64_t service_deadline,probe_deadline,stop_deadline,stage_at; };
static int preflight_bad,ready_value,start_bad,cleanup_bad,starts,cleanups;
static int fail(struct state *s,const char *e) {if(!s->error)s->error=e;return -1;}
int dlr_endpoints_preflight(struct dlr_endpoints *e) {e->initialized=1;return preflight_bad?-1:0;}
int dlr_endpoints_ready(struct dlr_endpoints *e,int32_t pid) {(void)e;assert(pid==77);return ready_value;}
int dlr_endpoints_cleanup(struct dlr_endpoints *e,int reaped) {assert(reaped);cleanups++;if(cleanup_bad)return -1;e->cleaned=1;return 0;}
int dlr_child_start(struct dlr_child *c,enum dlr_child_kind k,const char *n,uint64_t now) {assert(strlen(n)==48&&now);c->initialized=c->created=1;c->kind=k;c->pid=k==DLR_DAEMON?77:78;c->started=now;starts++;return start_bad?-1:0;}
void dlr_child_tick(struct dlr_child *c,uint64_t n) {(void)c;(void)n;}
int dlr_child_done(const struct dlr_child *c) {return c->created&&c->reaped&&!c->lost&&c->output_fd<0&&c->error_fd<0;}
'''
main=r'''
static struct state fresh(void) {struct state s;memset(&s,0,sizeof(s));memcpy(s.nonce,"0123456789abcdef0123456789abcdef0123456789abcdef",49);preflight_bad=start_bad=cleanup_bad=starts=cleanups=0;ready_value=0;return s;}
static void probe_pass(struct state *s) {
 int n=snprintf((char *)s->probe.output,sizeof(s->probe.output),"{\"version\":1,\"nonce\":\"%s\",\"name\":\"example.com\",\"pid\":78,\"status\":\"PASS\",\"gai_rc\":0,\"system_errno\":0,\"error\":\"none\",\"address_list_complete\":true,\"addresses\":[\"172.66.147.243\",\"104.20.23.154\"]}\n",s->nonce);assert(n>0);s->probe.output_used=(size_t)n;s->probe.reaped=s->probe.output_eof=s->probe.error_eof=1;s->probe.output_fd=s->probe.error_fd=-1;
}
int main(void) {
 struct state s=fresh();assert(!service_start(&s,100)&&s.phase==SERVICE_START&&starts==1);assert(!service_tick(&s,101)&&starts==1);ready_value=1;assert(!service_tick(&s,102)&&s.phase==RESOLVING&&starts==2);probe_pass(&s);assert(!service_tick(&s,103)&&s.phase==DNS_DRAIN&&s.resolver_ok&&s.selected==0xac4293f3&&s.resolver.count==2);
 s.phase=SERVICE_STOP;s.stop_deadline=6000;s.daemon.reaped=s.daemon.term_sent=s.daemon.output_eof=s.daemon.error_eof=1;s.daemon.output_fd=s.daemon.error_fd=-1;s.daemon.wait_status=EINTR<<8;assert(!service_tick(&s,1000)&&s.daemon_cleanup&&s.endpoints.cleaned&&cleanups==1);assert(!service_tick(&s,1001)&&cleanups==1);s.phase=CLOSE_WAIT;assert(!service_tick(&s,1002)&&cleanups==1);
 s=fresh();preflight_bad=1;assert(service_start(&s,100)<0&&!starts&&!strcmp(s.error,"resolver_endpoint_preflight"));
 s=fresh();start_bad=1;assert(service_start(&s,100)<0&&s.daemon.created&&!strcmp(s.error,"mdns_spawn"));
 s=fresh();assert(!service_start(&s,100));assert(service_tick(&s,5100)<0&&!strcmp(s.error,"mdns_endpoint_deadline"));
 s=fresh();assert(!service_start(&s,100));s.daemon.reaped=1;assert(service_tick(&s,101)<0&&!strcmp(s.error,"mdns_exited_early"));
 for(unsigned fault=0;fault<6;fault++) {s=fresh();assert(!service_start(&s,100));ready_value=1;assert(!service_tick(&s,101));probe_pass(&s);if(fault==0)s.probe.wait_status=9;else if(fault==1)s.probe.error_used=1;else if(fault==2)s.probe.output_eof=0;else if(fault==3)s.probe.output[0]='x';else if(fault==4)s.probe.output_used--;else s.probe.wait_status=1<<8;assert(service_tick(&s,102)<0&&!s.resolver_ok);}
 for(unsigned fault=0;fault<5;fault++){s=fresh();assert(!service_start(&s,100));s.phase=SERVICE_STOP;s.stop_deadline=6000;s.daemon.reaped=s.daemon.term_sent=s.daemon.output_eof=s.daemon.error_eof=1;s.daemon.output_fd=s.daemon.error_fd=-1;s.daemon.wait_status=EINTR<<8;if(fault==0)s.daemon.wait_status=0;else if(fault==1)s.daemon.wait_status=15;else if(fault==2)s.daemon.kill_sent=1;else if(fault==3)s.daemon.term_sent=0;else cleanup_bad=1;assert(service_tick(&s,102)<0&&!s.daemon_cleanup);}
 s=fresh();assert(!service_start(&s,100));ready_value=1;assert(!service_tick(&s,101));assert(service_tick(&s,15101)<0&&!strcmp(s.error,"native_resolver_deadline"));
 s=fresh();assert(!service_start(&s,100));s.phase=SERVICE_STOP;s.stop_deadline=1000;assert(service_tick(&s,1000)<0&&!strcmp(s.error,"mdns_reap_deadline"));
 puts("service transitions: 18 integrated success/failure scenarios passed");return 0;
}
'''
repo=ROOT.parents[3]
with tempfile.TemporaryDirectory(prefix='dlr-service-tests-') as d:
 p=Path(d);(p/'test.c').write_text(harness+body+main)
 command=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-fsanitize=address,undefined','-I'+str(ROOT),'-I'+str(repo/'integrations/puredarwin/chonkstep-darwin/darwin-spawn'),str(p/'test.c'),str(ROOT/'probe_record.c'),'-o',str(p/'test')]
 subprocess.run(command,check=True);subprocess.run([str(p/'test')],check=True)
