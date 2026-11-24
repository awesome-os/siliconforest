/* Original bounded parser of frontend007's closed launch journal. */
#include "proof_internal.h"
#include <stdio.h>
#include <string.h>
struct cursor {const uint8_t *p,*e;};
static int lit(struct cursor *c,const char *s) {size_t n=strlen(s);if((size_t)(c->e-c->p)<n||memcmp(c->p,s,n))return 0;c->p+=n;return 1;}
static int number(struct cursor *c,uint64_t *out) {const uint8_t *p=c->p;uint64_t n=0;while(c->p<c->e&&*c->p>='0'&&*c->p<='9') {unsigned d=(unsigned)(*c->p++-'0');if(n>(UINT64_MAX-d)/10)return 0;n=n*10+d;}if(c->p==p||(c->p-p>1&&*p=='0'))return 0;*out=n;return 1;}
static int line(struct cursor *c,struct cursor *l) {const uint8_t *e=memchr(c->p,'\n',(size_t)(c->e-c->p));if(!e||e==c->p||e-c->p>1024)return 0;*l=(struct cursor){c->p,e};c->p=e+1;return 1;}
static int exact(struct cursor c,const char *s) {return lit(&c,s)&&c.p==c.e;}
static const char *const gates[]={"Admission","StatusNonblocking","SpawnReturned","PidOwned","AppReady","AppCleanup","StatusEof","AppReaped","TermAttempt","KillAttempt"};
static int gate_times(const uint8_t *b,size_t n,uint64_t times[2][10],unsigned masks[2]) {
 struct cursor c={b,b+n},l;char prefix[96];
 while(c.p<c.e) {
  /* Snapshot summary can exceed journal's 1024 bound. */
  const uint8_t *e=memchr(c.p,'\n',(size_t)(c.e-c.p));if(!e)return 0;l=(struct cursor){c.p,e};c.p=e+1;
  for(unsigned a=0;a<2;a++)for(unsigned g=0;g<10;g++) {
   snprintf(prefix,sizeof(prefix),"attempt=%u gate=%s observed_ms=",a,gates[g]);struct cursor t=l;
   if(lit(&t,prefix)) {if(lit(&t,"Some(")) {if(!number(&t,&times[a][g])||!lit(&t,")")||t.p!=t.e||(masks[a]&(1u<<g)))return 0;masks[a]|=1u<<g;}else if(!exact(t,"None"))return 0;}
  }
 }
 return (masks[0]&255)==255&&(masks[1]&255)==255;
}
int dnds_manager_proof(const uint8_t *b,size_t n,const uint8_t *snapshot,size_t sn,const char *nonce,const struct dnds_session_summary *s) {
 if(!b||!n||n>65536)return 0;
 for(size_t i=0;i<n;i++)if((b[i]<32&&b[i]!='\n')||b[i]>126)return 0;
 uint64_t times[2][10]={{0}},lasttime=0,lastbytes[2]={0};unsigned expected[2]={0},seen[2]={0},requested[2]={0},native[2]={0},final[2]={0},diag[2]={0},manager_cs=0;
 if(!gate_times(snapshot,sn,times,expected))return 0;
 struct cursor c={b,b+n},l;char want[768];unsigned lines=0;
 if(!line(&c,&l)||!exact(l,"CHONKLAUNCH/1 scope=manager app_internal_gates=unknown_until_final_app_record"))return 0;
 while(c.p<c.e) {
  if(++lines>512||!line(&c,&l))return 0;
  struct cursor t=l;
  if(lit(&t,"CHONKLAUNCH/1 scope=manager self_cs_status=")) {if(manager_cs++||t.p==t.e)return 0;continue;}
  uint64_t a64;if(!lit(&t,"CHONKLAUNCH/1 attempt=")||!number(&t,&a64)||a64>1)return 0;unsigned a=(unsigned)a64;
  if(lit(&t," ms=")) {
   uint64_t ms;if(!number(&t,&ms)||ms<lasttime||ms>180000)return 0;lasttime=ms;
   snprintf(want,sizeof(want)," request=Some(%u) instance=Some(\"%04u%.44s\") source=explicit_startup event=requested",a+1,a+1,nonce);
   if(exact(t,want)) {if(requested[a]||ms!=s->app[a].created_ms||(a==1&&!(seen[0]&(1u<<4))))return 0;requested[a]=1;continue;}
   if(!requested[a]||!lit(&t," event="))return 0;
   unsigned g;for(g=0;g<10;g++)if(exact(t,gates[g]))break;
   if(g<10) {
    if(!(expected[a]&(1u<<g))||(seen[a]&(1u<<g))||ms!=times[a][g])return 0;
    if(g<4&&g&&!(seen[a]&(1u<<(g-1))))return 0;
    if(g==3&&!native[a])return 0;
    if(g>=4&&!(seen[a]&(1u<<3)))return 0;
    if(g>=5&&!(seen[a]&(1u<<4)))return 0;
    seen[a]|=1u<<g;continue;
   }
   /* These source diagnostic payloads do not determine a successful outcome.
    * Only their exact labels, bounded ASCII line and once-per-phase shape are
    * admitted; unknown event/failure/status lines never become diagnostics. */
   const char *labels[]={"CsStatus phase=Spawn cs_status=","CsStatus phase=Exit cs_status=","ExitReason exit_reason=","KernelMessages kernel_messages="};
   for(g=0;g<4;g++) {struct cursor q=t;if(lit(&q,labels[g])&&q.p<q.e)break;}
   if(g==4||(diag[a]&(1u<<g))||!(seen[a]&(1u<<3)))return 0;
   diag[a]|=1u<<g;continue;
  }
  if(!requested[a])return 0;
  snprintf(want,sizeof(want)," native_return=0 native=SpawnResultV1 { version: 1, stage: 10, pid: %d, primary_error: 0, spawn_error: 0, attr_destroy_error: 0, actions_destroy_error: 0, fd_close_error: 0, fd_close_failures: 0, requested_flags: 16398, observed_flags: 16398, child_created: 1, duplicate_count: 4, reserved: 0 }",s->app[a].pid);
  if(exact(t,want)) {if(native[a]||!(seen[a]&4)||(seen[a]&8))return 0;native[a]=1;continue;}
  uint64_t bytes;
  if(!lit(&t," status_bytes=")||!number(&t,&bytes)||bytes<lastbytes[a]||bytes>s->app[a].status_bytes||!(seen[a]&(1u<<4))||final[a])return 0;
  unsigned wait=0,cleanup=0;
  if(lit(&t," raw_app_wait=Some(0)"))wait=1;else if(!lit(&t," raw_app_wait=None"))return 0;
  if(!lit(&t," wait_error=None cleanup="))return 0;
  snprintf(want,sizeof(want),"Some(Cleanup { complete: true, raw_wait_status: %u, term_sent: %s, kill_sent: %s })",s->app[a].shell_wait,s->app[a].shell_term?"true":"false",s->app[a].shell_kill?"true":"false");
  if(exact(t,want))cleanup=1;else if(!exact(t,"None"))return 0;
  if((wait&&!(seen[a]&(1u<<7)))||(cleanup&&!(seen[a]&(1u<<5))))return 0;
  if(wait&&cleanup&&bytes==s->app[a].status_bytes&&(seen[a]&255)==255)final[a]=1;
  lastbytes[a]=bytes;
 }
 for(unsigned a=0;a<2;a++)if(!requested[a]||!native[a]||!final[a]||seen[a]!=expected[a])return 0;
 return 1;
}
