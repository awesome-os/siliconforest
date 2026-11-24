"""Execute the actual parent closing branches with deterministic external I/O.
The parent must resample the clock after observation output, not use loop time.
"""
from pathlib import Path
import subprocess,tempfile
G=Path(__file__).resolve().parent
source=(G/'net_resolver.c').read_text();a=source.index('  if(s->phase==SERVICE_STOP && s->daemon_cleanup');b=source.index('  struct pollfd f[4]',a);block=source[a:b]
pre=r'''
#include "close_guard.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { SERVICE_STOP=1,CLOSE_WAIT=2,DLNET_CLOSE=3 };
struct count { unsigned count; };
struct state {int phase,daemon_cleanup,daemon_recorded,daemon,rx,close_sent,peer_close;struct count output,input;size_t line_used;unsigned bit_count;uint64_t closing,close_deadline,deadline;struct dlr_drain drain;const char *error;};
static uint64_t clock_value;
static uint64_t ms(void){return clock_value++;}
static int can_transition(const struct state *s){return !s->output.count&&!s->input.count;}
static void child_observation(struct state *s,const char *n,const int *child){(void)s;(void)n;(void)child;clock_value+=7;}
static int emit(struct state *s,int type,const void *p,size_t n){(void)s;assert(type==DLNET_CLOSE&&!p&&!n);return 0;}
static int fail(struct state *s,const char *e){if(!s->error)s->error=e;return -1;}
static int dln_end(const int *p){(void)p;return 0;}
static void iteration(struct state *s,uint64_t now){(void)now;do {
'''
post=r'''
} while(0);}
int main(void){
 struct state s;memset(&s,0,sizeof(s));s.deadline=106000;s.phase=SERVICE_STOP;s.daemon_cleanup=1;clock_value=1000;
 iteration(&s,500);assert(!s.error&&s.close_sent&&s.phase==CLOSE_WAIT&&s.drain.state==1&&s.closing==1007&&s.close_deadline==41007&&s.drain.last==1008);
 s.peer_close=1;s.output.count=1;clock_value=32000;iteration(&s,501);assert(!s.error&&s.drain.state==1);
 s.output.count=0;clock_value=32001;iteration(&s,502);assert(!s.error&&s.drain.completed==32001&&s.drain.state==2);
 memset(&s,0,sizeof(s));s.deadline=106000;s.phase=SERVICE_STOP;s.daemon_cleanup=1;clock_value=1000;iteration(&s,500);clock_value=s.close_deadline;iteration(&s,501);assert(s.error&&!strcmp(s.error,"close_deadline")&&s.drain.error==DLR_DRAIN_TIMEOUT);
 puts("actual close branches: fresh clock, queuedACK, completedCLOSE and expiry PASS");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='dlr-close-integration-') as d:
 p=Path(d);c=p/'test.c';c.write_text(pre+block+post);binary=p/'test'
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-fsanitize=address,undefined','-I'+str(G),str(c),str(G/'close_guard.c'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
