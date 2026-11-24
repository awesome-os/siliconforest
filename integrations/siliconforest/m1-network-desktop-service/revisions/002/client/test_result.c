#ifdef NDEBUG
#error assertions required
#endif
#include "net_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct dn_result good(void){
 struct dn_result r;memset(&r,0,sizeof(r));
 const char *nonce="0123456789abcdef0123456789abcdef0123456789abcdef";
 const char *instance="00010123456789abcdef0123456789abcdef0123456789ab";
 char path[256];assert(snprintf(path,sizeof(path),DN_RECORD_PREFIX "%s/%s",nonce,instance)>0);
 assert(!dn_identity_parse(path,instance,&r.identity));
 r.pid=77;r.ppid=r.sid=r.pgid=r.foreground_pgid=76;r.error="none";
 r.address_count=2;r.address_list_complete=1;
 memcpy(r.addresses[0],"\x01\x01\x01\x01",4);memcpy(r.addresses[1],"\x5d\xb8\xd8\x22",4);
 memcpy(r.local,"\x0a\0\x02\x0f",4);memcpy(r.peer,r.addresses[0],4);
 r.local_port=49152;r.peer_port=80;r.entry_fd_checked=1;r.entry_fd_bound=256;
 r.tcp_maxseg_observed=256;r.request_shutdown=r.peer_eof=r.socket_created=r.socket_closed=1;
 assert(eg_http_request(nonce,48,r.request,sizeof(r.request),&r.request_bytes)==EG_OK);
 const char *body="<!doctype html><title>Example Domain</title>";
 int n=snprintf((char *)r.response,sizeof(r.response),"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",strlen(body),body);
 assert(n>0);r.response_bytes=(size_t)n;assert(eg_http_response(r.response,r.response_bytes,1,&r.http)==EG_OK);
 r.started_ms=1000;r.elapsed_ms=200;return r;
}
static void rejected(const struct dn_result *r){
 char out[DN_JSON_CAP],old[DN_JSON_CAP];memset(out,0xa5,sizeof(out));memcpy(old,out,sizeof(old));size_t n=123456;
 assert(dn_result_format(r,out,sizeof(out),&n)<0&&n==123456&&!memcmp(out,old,sizeof(out)));
}
int main(int argc,char **argv){
 struct dn_result base=good(),r;char out[DN_JSON_CAP];size_t n=0;
 assert(!dn_result_format(&base,out,sizeof(out),&n)&&n==strlen(out)&&out[n-1]=='\n');
 if(argc==2&&!strcmp(argv[1],"--emit")){fwrite(out,1,n,stdout);return 0;}
 assert(argc==1);
 for(unsigned f=0;f<43;f++){
  r=base;
  switch(f){
   case 0:r.identity.slot=2;break;case 1:r.identity.instance[4]='f';break;
   case 2:r.pid=1;break;case 3:r.pid=r.ppid;break;case 4:r.ppid=1;break;
   case 5:r.sid++;break;case 6:r.pgid++;break;case 7:r.foreground_pgid++;break;
   case 8:r.error="unknown";break;case 9:r.error_errno=1;break;case 10:r.gai_rc=-2;break;
   case 11:r.system_errno=5;break;case 12:r.address_count=0;break;case 13:r.address_count=33;break;
   case 14:r.address_list_complete=0;break;case 15:r.address_list_complete=2;break;
   case 16:r.addresses[1][0]=127;break;case 17:r.peer[0]=2;break;
   case 18:r.local[3]=16;break;case 19:r.local_port=1023;break;case 20:r.peer_port=81;break;
   case 21:r.entry_fd_checked=0;break;case 22:r.entry_fd_checked=2;break;case 23:r.entry_fd_bound=2;break;
   case 24:r.entry_fd_bound=65537;break;case 25:r.tcp_maxseg_observed=1460;break;
   case 26:r.request_bytes=185;break;case 27:r.request[0]='P';break;
   case 28:r.request_shutdown=0;break;case 29:r.request_shutdown=2;break;
   case 30:r.peer_eof=0;break;case 31:r.peer_eof=2;break;case 32:r.socket_created=0;break;
   case 33:r.socket_closed=0;break;case 34:r.response_bytes=0;break;case 35:r.response_bytes=2049;break;
   case 36:r.response[9]='5';break;case 37:r.http.body_bytes++;break;case 38:r.http.eof_observed=0;break;
   case 39:r.http.framing_complete=0;break;case 40:r.http.example_content=0;break;
   case 41:r.started_ms=0;break;case 42:r.elapsed_ms=60000;break;
  }
  rejected(&r);
 }
 for(size_t cap=0;cap<=n;cap++){
  size_t written=123;char old[DN_JSON_CAP];memset(out,0x5a,sizeof(out));memcpy(old,out,sizeof(out));
  assert(dn_result_format(&base,out,cap,&written)<0&&written==123&&!memcmp(out,old,sizeof(out)));
 }
 r=base;r.error="gai";r.gai_rc=-2;r.address_list_complete=0;r.address_count=0;
 assert(!dn_result_format(&r,out,sizeof(out),&n)&&strstr(out,"\"status\":\"FAILED\""));
 struct dn_identity id,old;memset(&id,0x5a,sizeof(id));old=id;
 const char *bad[]={"/tmp/session-0123456789abcdef0123456789abcdef0123456789abcdef/00010123456789abcdef0123456789abcdef0123456789ab",
 DN_RECORD_PREFIX "0123456789abcdef0123456789abcdef0123456789abcdef/00030123456789abcdef0123456789abcdef0123456789ab",
 DN_RECORD_PREFIX "0123456789abcdef0123456789abcdef0123456789abcdef/00010123456789abcdef0123456789abcdef0123456789ab/extra"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(dn_identity_parse(bad[i],base.identity.instance,&id)<0&&!memcmp(&id,&old,sizeof(id)));
 puts("43 failed-PASS field cases, every short output cap, strict identity/output preservation passed");return 0;
}
