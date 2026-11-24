/* Original closed-record composition. License selection pending. */
#include "proof_internal.h"
#include "../../m1-network-egress-guest/codec/egress_validate.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DNDS_PROOF_HOST_TEST
#include <openssl/sha.h>
#define CC_SHA256(p,n,out) SHA256((p),(n),(out))
#else
#include <CommonCrypto/CommonDigest.h>
#endif
#define PROFILE "desktop-native-resolver-two-http-v1"
#define PREFIX "/private/var/run/darwin-desktop-network/session-"
struct saved_record { struct rg_snapshot snapshot; unsigned slot; const char *file; uint8_t digest[32]; };
struct work {
 struct saved_record saved[24]; unsigned saved_n;
 struct rg_read_workspace read;
 struct rg_json_workspace json;
 struct rg_document doc;
 struct rg_identity dirs[3]; unsigned have[3];
 char session[128],nonce[49];
 uint8_t session_raw[16384]; size_t session_n, observation_bytes;
 void (*observe)(const char *,const uint8_t *,size_t,void *); void *ctx;
};
static int identity(struct rg_identity a,struct rg_identity b) {return a.device==b.device&&a.inode==b.inode;}
/* Exact encoded line accounting matching the caller's 64-byte chunks. */
static int emit(struct work *w,const char *kind,const uint8_t *b,size_t n) {
 size_t pos=0;char prefix[192];
 do {
  size_t part=n-pos;if(part>64)part=64;
  int k=snprintf(prefix,sizeof(prefix),"DL30_OBSERVE_%s kind=%s total=%zu offset=%zu hex=",w->nonce,kind,n,pos);
  if(k<0||(size_t)k>=sizeof(prefix)||w->observation_bytes>524288-(size_t)k-2*part-1)return 0;
  w->observation_bytes+=(size_t)k+2*part+1;pos+=part;
 }while(pos<n);
 w->observe(kind,b,n,w->ctx);return 1;
}
static int read_record(struct work *w,unsigned slot,const char *file,const char *kind,struct rg_snapshot *s) {
 uint8_t before[32],after[32];char digest_kind[64];
 if(rg_read(w->session,w->nonce,slot,file,&w->read,s)!=RG_OK||s->length>65536)return 0;
 if(w->have[slot]&&!identity(w->dirs[slot],s->directory))return 0;
 if(!w->have[slot]) {w->dirs[slot]=s->directory;w->have[slot]=1;}
 if(w->saved_n>=24||!CC_SHA256(s->bytes,(unsigned)s->length,before))return 0;
 struct saved_record *saved=&w->saved[w->saved_n++];saved->snapshot=*s;saved->slot=slot;saved->file=file;memcpy(saved->digest,before,32);
 if(!emit(w,kind,s->bytes,s->length))return 0;
 if(!CC_SHA256(s->bytes,(unsigned)s->length,after)||memcmp(before,after,32))return 0;
 int n=snprintf(digest_kind,sizeof(digest_kind),"%s_sha256",kind);
 return n>0&&(size_t)n<sizeof(digest_kind)&&emit(w,digest_kind,before,32);
}
static int json_record(struct work *w,unsigned slot,const char *file,const char *kind) {
 struct rg_snapshot s;
 return read_record(w,slot,file,kind,&s)&&rg_json_parse(s.bytes,s.length,&w->json,&w->doc)==RG_OK&&w->doc.tokens[0].type==RG_OBJECT;
}
/* Paths here are fixed source literals, never input expressions. */
static int at(const struct rg_document *d,const char *path,uint32_t *index) {
 struct rg_step steps[4];size_t n=0;const char *p=path;
 do { const char *end=strchr(p,'.');size_t len=end?(size_t)(end-p):strlen(p);
  if(!len||n==4)return 0;
  steps[n++]=(struct rg_step){RG_KEY,p,len,0};p=end?end+1:NULL;
 }while(p);
 return rg_json_path(d,steps,n,index)==RG_OK;
}
static int num(const struct rg_document *d,const char *p,uint64_t *n) {uint32_t i;return at(d,p,&i)&&rg_json_u64(d,i,n)==RG_OK;}
static int eqn(const struct rg_document *d,const char *p,uint64_t expected) {uint64_t n;return num(d,p,&n)&&n==expected;}
static int range(const struct rg_document *d,const char *p,uint64_t lo,uint64_t hi,uint64_t *out) {uint64_t n;if(!num(d,p,&n)||n<lo||n>hi)return 0;if(out)*out=n;return 1;}
static int boolean(const struct rg_document *d,const char *p,int v) {uint32_t i;int b;return at(d,p,&i)&&rg_json_bool(d,i,&b)==RG_OK&&b==v;}
static int text(const struct rg_document *d,const char *p,const char *value) {char b[256];size_t n;uint32_t i;return at(d,p,&i)&&rg_json_string(d,i,b,sizeof(b),&n)==RG_OK&&n==strlen(value)&&!memcmp(b,value,n);}
static int nil(const struct rg_document *d,const char *p) {uint32_t i;return at(d,p,&i)&&rg_json_null(d,i)==RG_OK;}
static int boundary(const struct rg_document *d,const char *prefix,uint64_t *bound) {
 char key[128]; const char *flags[]={"pre_exec_fd_check_observed","exec_status_pipe_eof_observed","shell_exec_checked"};
 for(unsigned j=0;j<3;j++) {snprintf(key,sizeof(key),"%s.%s",prefix,flags[j]);if(!boolean(d,key,1))return 0;}
 snprintf(key,sizeof(key),"%s.inventory_scope",prefix);if(!text(d,key,"pre_exec_only_not_after_exec_inventory"))return 0;
 snprintf(key,sizeof(key),"%s.pre_exec_fd_scan_bound_exclusive",prefix);return range(d,key,3,1048576,bound);
}
static int app_result(struct work *w,const char *instance,unsigned slot,const struct dnds_app_summary *a,struct dnds_desktop_facts *f,struct proof_app *pa) {
 const struct rg_document *d=&w->doc;uint64_t shell,client,b2,ignored;uint32_t errors;
 if(!eqn(d,"schema",1)||!text(d,"instance",instance)||!eqn(d,"app_pid",(uint32_t)a->pid)||!text(d,"shell","/bin/sh")||!text(d,"term","dumb")||
 !range(d,"child_pid",2,INT32_MAX,&shell)||!boundary(d,"child_exec_boundary",&pa->fd_bound)||!boundary(d,"session.exec_boundary",&b2)||b2!=pa->fd_bound||
 !boolean(d,"cleanup.complete",1)||!eqn(d,"cleanup.status",a->shell_wait)||!boolean(d,"cleanup.term_sent",(int)a->shell_term)||!boolean(d,"cleanup.kill_sent",(int)a->shell_kill)||!nil(d,"cleanup.error")||
 !at(d,"errors",&errors)||d->tokens[errors].type!=RG_ARRAY||d->tokens[errors].children!=0)return 0;
 const char *ready[]={"session.ready","session.keymap","session.pty_exec_checked","session.ssd","startup.client_record_complete","startup.client_success","startup.shell_return_observed","startup.client_reaped_by_shell","startup.fallback_prompt_observed","startup.foreground_matches_shell","startup.complete"};
 for(size_t i=0;i<sizeof(ready)/sizeof(ready[0]);i++)if(!boolean(d,ready[i],1))return 0;
 if(!range(d,"session.configure_serial",1,UINT32_MAX,NULL)||!range(d,"session.frames",1,UINT64_MAX,NULL)||!range(d,"session.commits",1,UINT64_MAX,NULL)||
 !range(d,"session.pty_rx_bytes",1,65536,&pa->rx)||!range(d,"session.pty_tx_bytes",0,65536,&pa->tx)||!eqn(d,"session.input_accepted_bytes",pa->tx)||!eqn(d,"session.input_unsent_bytes",0)||!num(d,"session.unsupported_terminal_sequences",&ignored)||
 !eqn(d,"startup.version",1)||!text(d,"startup.profile",PROFILE)||!text(d,"startup.session_nonce",w->nonce)||!text(d,"startup.instance",instance)||!eqn(d,"startup.slot",slot)||!eqn(d,"startup.shell_pid",shell)||!range(d,"startup.client_pid",2,INT32_MAX,&client)||!eqn(d,"startup.client_exit_status",0)||!eqn(d,"startup.foreground_pgid",shell)||!text(d,"startup.error","none"))return 0;
 /* Successful source stop modes; a signal remains visible, never relabeled EOF. */
 int closed=boolean(d,"session.window_closed",1),eof=boolean(d,"session.pty_eof",1);
 if(!(closed||boolean(d,"session.window_closed",0))||!(eof||boolean(d,"session.pty_eof",0))||!(boolean(d,"session.term_signal",0)||boolean(d,"session.term_signal",1)))return 0;
 const char *cause=closed?"window_close":eof?"shell_eof":boolean(d,"session.term_signal",1)?"signal":"deadline";
 if(!text(d,"session.stop_cause",cause))return 0;
 f->app_pid[slot-1]=a->pid;f->shell_pid[slot-1]=(int32_t)shell;f->client_pid[slot-1]=(int32_t)client;
 return 1;
}
static int ipv4(const char *s,size_t n,uint8_t out[4]) {
 size_t p=0;
 for(unsigned i=0;i<4;i++) {unsigned value=0,count=0;size_t start=p;
  while(p<n&&s[p]>='0'&&s[p]<='9') {if(++count>3)return 0;value=value*10+(unsigned)(s[p++]-'0');}
  if(!count||value>255||(count>1&&s[start]=='0'))return 0;out[i]=(uint8_t)value;
  if(i<3&&(p==n||s[p++]!='.'))return 0;
 }
 return p==n;
}
static int ipfield(const struct rg_document *d,const char *p,uint8_t out[4]) {char b[16];size_t n;uint32_t i;return at(d,p,&i)&&rg_json_string(d,i,b,sizeof(b),&n)==RG_OK&&ipv4(b,n,out);}
static int unhex(const struct rg_document *d,const char *p,uint8_t *out,size_t cap,size_t *len) {
 char *b=malloc(4097);size_t n;uint32_t i;int okay=0;
 if(!b)return 0;
 if(!at(d,p,&i)||rg_json_string(d,i,b,4097,&n)!=RG_OK||n%2||n/2>cap)goto done;
 for(size_t j=0;j<n;j++) {unsigned x=(unsigned char)b[j];if(x>='0'&&x<='9')x-='0';else if(x>='a'&&x<='f')x=x-'a'+10;else goto done;
  if(j%2)out[j/2]|=(uint8_t)x;else out[j/2]=(uint8_t)(x<<4);
 }*len=n/2;okay=1;
done:free(b);return okay;
}
static int client_result(struct work *w,const char *instance,unsigned slot,const struct dnds_desktop_facts *f,struct proof_app *pa) {
 const struct rg_document *d=&w->doc;uint32_t ai;uint8_t ip[4],local[4];uint64_t port,start,elapsed;char b[16];size_t n;
 if(!eqn(d,"version",1)||!text(d,"profile",PROFILE)||!text(d,"nonce",w->nonce)||!text(d,"instance",instance)||!eqn(d,"slot",slot)||!eqn(d,"pid",(uint32_t)f->client_pid[slot-1]))return 0;
 const char *pids[]={"ppid","sid","pgid","foreground_pgid"};for(unsigned i=0;i<4;i++)if(!eqn(d,pids[i],(uint32_t)f->shell_pid[slot-1]))return 0;
 if(!text(d,"status","PASS")||!text(d,"error","none")||!eqn(d,"error_errno",0)||!eqn(d,"gai_rc",0)||!eqn(d,"system_errno",0))return 0;
 const char *truth[]={"address_list_complete","entry_fd_check_observed","request_shutdown","peer_eof","socket_created","socket_closed","http.framing_complete","http.eof_observed","http.example_content"};
 for(size_t i=0;i<sizeof(truth)/sizeof(truth[0]);i++)if(!boolean(d,truth[i],1))return 0;
 if(!range(d,"entry_fd_scan_bound_exclusive",3,65536,NULL)||!eqn(d,"tcp_maxseg_request",256)||!eqn(d,"tcp_maxseg_observed",256)||!at(d,"addresses",&ai)||d->tokens[ai].type!=RG_ARRAY||d->tokens[ai].children<1||d->tokens[ai].children>32)return 0;
 uint32_t entry=ai+1;for(unsigned i=0;i<d->tokens[ai].children;i++) {
  if(rg_json_string(d,entry,b,sizeof(b),&n)!=RG_OK||!ipv4(b,n,ip)||!eg_ipv4_global(ip))return 0;
  if(!i)memcpy(pa->peer,ip,4);entry=d->tokens[entry].next;
 }
 if(!ipfield(d,"selected",ip)||memcmp(ip,pa->peer,4)||!ipfield(d,"peer.address",ip)||memcmp(ip,pa->peer,4)||!eqn(d,"peer.port",80)||!ipfield(d,"local.address",local)||memcmp(local,"\x0a\0\x02\x0f",4)||!range(d,"local.port",1024,65535,&port)||!range(d,"started_ms",1,UINT64_MAX,&start)||!range(d,"elapsed_ms",0,59999,&elapsed)||start>UINT64_MAX-elapsed)return 0;
 pa->port=(uint16_t)port;
 /* The only sizable scratch in this function is allocated, not kernel/guest stack. */
 uint8_t *response=malloc(2048);if(!response)return 0;
 uint8_t request[186],expected[186];size_t rn=0,qn=0,en=0;struct eg_http_facts http;int okay=0;
 if(unhex(d,"request_hex",request,sizeof(request),&qn)&&unhex(d,"response_hex",response,2048,&rn)&&qn==186&&rn>0&&eqn(d,"request_bytes",qn)&&eqn(d,"response_bytes",rn)&&eg_http_request(w->nonce,48,expected,sizeof(expected),&en)==EG_OK&&en==qn&&!memcmp(request,expected,qn)&&eg_http_response(response,rn,1,&http)==EG_OK&&eqn(d,"http.status",200)&&eqn(d,"http.header_bytes",http.header_bytes)&&eqn(d,"http.body_bytes",http.body_bytes)&&eqn(d,"http.framing",http.framing)&&http.framing_complete&&http.eof_observed&&http.example_content)okay=1;
 free(response);return okay;
}
static int pty_proof(const uint8_t *b,size_t n,const char *marker,size_t mn,const char *nonce,const char *instance,int32_t shell,int32_t client) {
 size_t p=0,end_marker=0;unsigned stage=0;char begin[256],done[256];
 int bn=snprintf(begin,sizeof(begin),"DL30_CLIENT_BEGIN session=%s instance=%s pid=%d shell_pid=%d",nonce,instance,client,shell);
 int dn=snprintf(done,sizeof(done),"DL30_CLIENT_DONE session=%s instance=%s pid=%d status=0",nonce,instance,client);
 if(bn<1||dn<1||(size_t)bn>=sizeof(begin)||(size_t)dn>=sizeof(done))return 0;
 while(p<n) {const uint8_t *end=memchr(b+p,'\n',n-p);if(!end)break;size_t e=(size_t)(end-b),clean=e;while(clean>p&&b[clean-1]=='\r')clean--;
  if(clean-p>=17&&!memcmp(b+p,"DL30_CLIENT_BEGIN",17)) {if(stage!=0||clean-p!=(size_t)bn||memcmp(b+p,begin,(size_t)bn))return 0;stage=1;}
  if(clean-p>=16&&!memcmp(b+p,"DL30_CLIENT_DONE",16)) {if(stage!=1||clean-p!=(size_t)dn||memcmp(b+p,done,(size_t)dn))return 0;stage=2;}
  if(clean-p>=17&&!memcmp(b+p,"DL30_SHELL_RETURN",17)) {
   if(stage!=2||clean-p!=mn-1||memcmp(b+p,marker,mn-1))return 0;stage=3;end_marker=e+1;
  }p=e+1;
 }
 static const uint8_t prompt[]="$ \x1b[?2004h";
 if(stage!=3)return 0;
 for(size_t i=end_marker;i+sizeof(prompt)-1<=n;i++)if(!memcmp(b+i,prompt,sizeof(prompt)-1))return 1;
 return 0;
}
static int grid_proof(const uint8_t *b,size_t n) {
 unsigned rows=1,cols=0,width=0;
 for(size_t p=0;p<n;p++) {
  if(b[p]=='\n') {if(!width)width=cols;if(cols!=width||++rows>60)return 0;cols=0;}
  else if(b[p]>=32&&b[p]<=126)cols++;
  else if(p+2<n&&b[p]==0xef&&b[p+1]==0xbf&&b[p+2]==0xbd) {cols++;p+=2;}
  else return 0;
  if(cols>160)return 0;
 }
 return rows>=6&&rows<=60&&width>=20&&width<=160&&cols==width;
}
static int same_time(struct rg_time a,struct rg_time b) {return a.seconds==b.seconds&&a.nanoseconds==b.nanoseconds;}
static int recheck(struct work *w) {
 for(unsigned i=0;i<w->saved_n;i++) {struct saved_record *r=&w->saved[i];struct rg_snapshot s;uint8_t digest[32];
  if(rg_read(w->session,w->nonce,r->slot,r->file,&w->read,&s)!=RG_OK||!identity(s.directory,r->snapshot.directory)||!identity(s.file,r->snapshot.file)||s.length!=r->snapshot.length||!same_time(s.mtime,r->snapshot.mtime)||!same_time(s.ctime,r->snapshot.ctime)||!CC_SHA256(s.bytes,(unsigned)s.length,digest)||memcmp(digest,r->digest,32))return 0;
 }return 1;
}
int dnds_verify_closed(const char nonce[49],int32_t frontend_pid,void (*observe)(const char *,const uint8_t *,size_t,void *),void *ctx,struct dnds_desktop_facts *out) {
 if(!nonce||!out||!observe||frontend_pid<2)return -1;
 for(unsigned i=0;i<48;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return -1;
 if(nonce[48])return -1;
 struct work *w=calloc(1,sizeof(*w));if(!w)return -1;
 memcpy(w->nonce,nonce,49);snprintf(w->session,sizeof(w->session),PREFIX"%s",nonce);w->observe=observe;w->ctx=ctx;
 int result=-1;struct rg_snapshot s;struct dnds_session_summary session;struct dnds_desktop_facts facts;struct proof_app apps[2];memset(&facts,0,sizeof(facts));memset(apps,0,sizeof(apps));
 if(!read_record(w,0,"session-result.txt","session_result",&s)||!dnds_session_text(s.bytes,s.length,nonce,&session))goto done;
 memcpy(w->session_raw,s.bytes,s.length);w->session_n=s.length;
 if(!read_record(w,0,"manager-status.log","manager_status",&s)||!dnds_manager_proof(s.bytes,s.length,w->session_raw,w->session_n,nonce,&session))goto done;
 for(unsigned slot=1;slot<=2;slot++) {
  char instance[49],kind[64],marker[256],pidline[32];uint64_t bound;snprintf(instance,sizeof(instance),"%04u%.44s",slot,nonce);
#define JSON(file,key) (snprintf(kind,sizeof(kind),"app%u_%s",slot,key),json_record(w,slot,file,kind))
#define RAW(file,key) (snprintf(kind,sizeof(kind),"app%u_%s",slot,key),read_record(w,slot,file,kind,&s))
  if(!JSON("result.json","result")||!app_result(w,instance,slot,&session.app[slot-1],&facts,&apps[slot-1]))goto done;
  int32_t shell=facts.shell_pid[slot-1],client=facts.client_pid[slot-1],app=facts.app_pid[slot-1];
  if(!JSON("child-ready.json","child_ready")||!eqn(&w->doc,"parent_pid",(uint32_t)app)||!eqn(&w->doc,"child_pid",(uint32_t)shell)||!eqn(&w->doc,"sid",(uint32_t)shell)||!eqn(&w->doc,"pgid",(uint32_t)shell)||!boolean(&w->doc,"session_and_ctty_checked_in_child",1)||!boolean(&w->doc,"exec_status_pipe_closed",1)||!text(&w->doc,"shell","/bin/sh")||!boundary(&w->doc,"exec_boundary",&bound)||bound!=apps[slot-1].fd_bound)goto done;
  if(!JSON("child.json","child")||!eqn(&w->doc,"parent_pid",(uint32_t)app)||!eqn(&w->doc,"child_pid",(uint32_t)shell)||!boolean(&w->doc,"setup_checked",0)||!text(&w->doc,"job_control","disabled; foreground group must remain owned shell"))goto done;
  if(!JSON("network-client.json","client")||!client_result(w,instance,slot,&facts,&apps[slot-1]))goto done;
  int pn=snprintf(pidline,sizeof(pidline),"%d\n",client);
  if(pn<1||!RAW("network-client.pid","client_pid")||s.length!=(size_t)pn||memcmp(s.bytes,pidline,s.length))goto done;
  int mn=snprintf(marker,sizeof(marker),"DL30_SHELL_RETURN session=%s instance=%s shell_pid=%d client_pid=%d status=0\n",nonce,instance,shell,client);
  if(mn<1||(size_t)mn>=sizeof(marker)||!RAW("network-shell-return.txt","shell_return")||s.length!=(size_t)mn||memcmp(s.bytes,marker,s.length))goto done;
  if(!RAW("pty-rx.bin","pty_rx")||s.length!=apps[slot-1].rx||!pty_proof(s.bytes,s.length,marker,(size_t)mn,nonce,instance,shell,client))goto done;
  if(!RAW("pty-tx.bin","pty_tx")||s.length!=apps[slot-1].tx)goto done;
  if(!RAW("grid-final.txt","grid")||!grid_proof(s.bytes,s.length))goto done;
  if(!RAW("application-stdout.log","stdout")||s.length||!RAW("application-stderr.log","stderr")||s.length)goto done;
  char status[256];int sn=snprintf(status,sizeof(status),"CHONKTERM/1 READY %s %d\nCHONKTERM/1 CLEANUP %s %d 1 %u %u %u\n",instance,app,instance,app,session.app[slot-1].shell_wait,session.app[slot-1].shell_term,session.app[slot-1].shell_kill);
  if(sn<0||(uint64_t)sn!=session.app[slot-1].status_bytes)goto done;
#undef JSON
#undef RAW
 }
 if(identity(w->dirs[0],w->dirs[1])||identity(w->dirs[0],w->dirs[2])||identity(w->dirs[1],w->dirs[2]))goto done;
 int32_t ids[]={frontend_pid,facts.app_pid[0],facts.app_pid[1],facts.shell_pid[0],facts.shell_pid[1],facts.client_pid[0],facts.client_pid[1]};
 for(unsigned i=0;i<7;i++)for(unsigned j=0;j<i;j++)if(ids[i]==ids[j])goto done;
 if(apps[0].port==apps[1].port&&!memcmp(apps[0].peer,apps[1].peer,4))goto done;
 if(!recheck(w))goto done;
 *out=facts;result=0;
done:free(w);return result;
}
