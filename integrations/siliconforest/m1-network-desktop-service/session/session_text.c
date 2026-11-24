#include "session_text.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
struct cursor { const uint8_t *p,*end; };
static int lit(struct cursor *c,const char *s) {
 size_t n=strlen(s);
 if((size_t)(c->end-c->p)<n||memcmp(c->p,s,n))return 0;
 c->p+=n;return 1;
}
static int number(struct cursor *c,uint64_t *out) {
 const uint8_t *start=c->p;uint64_t n=0;
 while(c->p<c->end&&*c->p>='0'&&*c->p<='9') {
  unsigned d=(unsigned)(*c->p-'0');
  if(n>(UINT64_MAX-d)/10)return 0;
  n=n*10+d;c->p++;
 }
 if(c->p==start||(c->p-start>1&&*start=='0'))return 0;
 *out=n;return 1;
}
static int boolean(struct cursor *c,unsigned *out) {
 if(lit(c,"true")){*out=1;return 1;}
 if(lit(c,"false")){*out=0;return 1;}
 return 0;
}
static int line(struct cursor *c,struct cursor *out) {
 const uint8_t *e=memchr(c->p,'\n',(size_t)(c->end-c->p));
 if(!e||e==c->p||e-c->p>4096)return 0;
 out->p=c->p;out->end=e;c->p=e+1;return 1;
}
static int header(struct cursor *c,struct dnds_session_summary *s) {
 struct cursor l;
 if(!line(c,&l)||!lit(&l,"CHONKDESKTOP/1 initialized=1 input=both shell=shared native_runtime_qualification=not_inferred")||l.p!=l.end)return 0;
 if(!line(c,&l)||!lit(&l,"CHONKDESKTOP/1 elapsed_ms=")||!number(&l,&s->elapsed_ms)||
    s->elapsed_ms>180000||(!lit(&l," cleanup=Complete deadline=")||!boolean(&l,&s->deadline_reached)||!lit(&l," frame_error=None manager_failed=false"))||l.p!=l.end)return 0;
 return line(c,&l)&&lit(&l,"CHONKLAUNCH/1 snapshot attempts=2 journal_error=None app_internal_gates=unknown_until_final_app_record")&&l.p==l.end;
}
static int application(struct cursor *c,unsigned slot,const char *nonce,struct dnds_app_summary *s) {
 struct cursor l;char prefix[160],instance[49];uint64_t pid,wait,spawn_pid;
 int n=snprintf(instance,sizeof(instance),"%04u%.44s",slot,nonce);
 if(n!=48)return 0;
 n=snprintf(prefix,sizeof(prefix),"attempt=%u source=explicit_startup request=Some(%u) instance=Some(\"%s\") pid=Some(",slot-1,slot,instance);
 if(n<0||(size_t)n>=sizeof(prefix)||!line(c,&l)||!lit(&l,prefix)||!number(&l,&pid)||pid<2||pid>INT32_MAX||
    !lit(&l,") created_ms=")||!number(&l,&s->created_ms)||s->created_ms>180000||
    !lit(&l," failure=None failure_ms=None status_bytes=")||!number(&l,&s->status_bytes)||!s->status_bytes||s->status_bytes>4096||
    !lit(&l," wait=Some(0) wait_error=None cleanup=Some(Cleanup { complete: true, raw_wait_status: ")||
    !number(&l,&wait)||wait>65535||!lit(&l,", term_sent: ")||!boolean(&l,&s->shell_term)||
    !lit(&l,", kill_sent: ")||!boolean(&l,&s->shell_kill)||
    (s->shell_kill&&!s->shell_term)||
    !lit(&l," }) native_return=Some(0) native=Some(SpawnResultV1 { version: 1, stage: 10, pid: ")||
    !number(&l,&spawn_pid)||spawn_pid!=pid||
    !lit(&l,", primary_error: 0, spawn_error: 0, attr_destroy_error: 0, actions_destroy_error: 0, fd_close_error: 0, fd_close_failures: 0, requested_flags: 16398, observed_flags: 16398, child_created: 1, duplicate_count: 4, reserved: 0 })")||l.p!=l.end)return 0;
 s->pid=(int32_t)pid;s->shell_wait=(uint32_t)wait;
 /* Diagnostics are not verdict inputs. Accept only the three exact bounded
  * line labels emitted by the same successful snapshot serializer. */
 const char *diagnostics[]={"exit_reason=","cs_at_spawn=","kernel_messages="};
 for(unsigned i=0;i<3;i++) {
  n=snprintf(prefix,sizeof(prefix),"attempt=%u %s",slot-1,diagnostics[i]);
  if(n<0||(size_t)n>=sizeof(prefix)||!line(c,&l)||!lit(&l,prefix)||l.p==l.end)return 0;
 }
 const char *gates[]={"Admission","StatusNonblocking","SpawnReturned","PidOwned",
                      "AppReady","AppCleanup","StatusEof","AppReaped","TermAttempt","KillAttempt"};
 uint64_t times[10]={0};unsigned seen[10]={0};
 for(unsigned i=0;i<10;i++) {
  n=snprintf(prefix,sizeof(prefix),"attempt=%u gate=%s observed_ms=",slot-1,gates[i]);
  if(n<0||(size_t)n>=sizeof(prefix)||!line(c,&l)||!lit(&l,prefix))return 0;
  if(lit(&l,"Some(")) {
   if(!number(&l,&times[i])||times[i]>180000||!lit(&l,")"))return 0;
   seen[i]=1;
  }else if(i<8||!lit(&l,"None"))return 0;
  if(l.p!=l.end)return 0;
 }
 for(unsigned i=0;i<8;i++)if(!seen[i]||times[i]<s->created_ms)return 0;
 /* Snapshot order differs from chronological order for the status events. */
 if(times[0]>times[1]||times[1]>times[2]||times[2]>times[3]||times[3]>times[4]||
    times[4]>times[5]||times[4]>times[6]||times[4]>times[7]||
    (seen[8]&&(times[8]<times[4]||times[8]>times[7]))||seen[9])return 0;
 s->ready_ms=times[4];s->cleanup_ms=times[5];s->reaped_ms=times[7];return 1;
}
int dnds_session_text(const uint8_t *bytes,size_t n,const char nonce[49],struct dnds_session_summary *out) {
 if(!bytes||!n||n>16384||!nonce||strlen(nonce)!=48||!out)return 0;
 for(size_t i=0;i<48;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return 0;
 for(size_t i=0;i<n;i++)if((bytes[i]<32&&bytes[i]!='\n')||bytes[i]>126)return 0;
 struct cursor c={bytes,bytes+n};struct dnds_session_summary s;memset(&s,0,sizeof(s));
 if(!header(&c,&s)||!application(&c,1,nonce,&s.app[0])||!application(&c,2,nonce,&s.app[1])||c.p!=c.end||
    s.app[0].pid==s.app[1].pid||s.app[0].reaped_ms>s.elapsed_ms||s.app[1].reaped_ms>s.elapsed_ms)return 0;
 *out=s;return 1;
}
