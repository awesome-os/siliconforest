/* Original pure bounded identity/serializer; never calls a resolver. */
#include "net_client.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
int dn_hex48(const char *p){if(!p||strlen(p)!=48)return 0;for(unsigned i=0;i<48;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return 1;}
int dn_identity_parse(const char *path,const char *instance,struct dn_identity *out){
 size_t prefix=sizeof(DN_RECORD_PREFIX)-1;
 if(!path||!out||!dn_hex48(instance)||strlen(path)!=prefix+48+1+48||strncmp(path,DN_RECORD_PREFIX,prefix)||path[prefix+48]!='/')return -1;
 struct dn_identity value;memset(&value,0,sizeof(value));memcpy(value.nonce,path+prefix,48);memcpy(value.instance,instance,49);
 if(!dn_hex48(value.nonce)||strcmp(path+prefix+49,instance)||memcmp(instance+4,value.nonce,44))return -1;
 if(!memcmp(instance,"0001",4))value.slot=1;else if(!memcmp(instance,"0002",4))value.slot=2;else return -1;
 *out=value;return 0;
}
static int append(char *p,size_t cap,size_t *used,const char *fmt,...){if(*used>=cap)return -1;va_list args;va_start(args,fmt);int n=vsnprintf(p+*used,cap-*used,fmt,args);va_end(args);if(n<0||(size_t)n>=cap-*used)return -1;*used+=(size_t)n;return 0;}
static int ip(char *p,size_t cap,size_t *used,const uint8_t a[4]){return append(p,cap,used,"\"%u.%u.%u.%u\"",a[0],a[1],a[2],a[3]);}
static int hex(char *p,size_t cap,size_t *used,const uint8_t *data,size_t n){static const char digits[]="0123456789abcdef";if(*used>=cap||n>(cap-*used-1)/2)return -1;for(size_t i=0;i<n;i++){p[(*used)++]=digits[data[i]>>4];p[(*used)++]=digits[data[i]&15];}p[*used]=0;return 0;}
int dn_result_format(const struct dn_result *r,char *out,size_t cap,size_t *length){
 static const char *const errors[]={"none","identity","entry_fd","tty","gai","address","deadline","socket","connect","send","shutdown","recv","http","tuple","close","record","signal"};int known=0;
 if(!r||!out||!length||!dn_hex48(r->identity.nonce)||!dn_hex48(r->identity.instance)||r->identity.slot<1||r->identity.slot>2||r->pid<2||r->ppid<2||r->address_count>32||r->request_bytes>186||r->response_bytes>2048||r->local_port>65535||r->peer_port>65535||r->error_errno<0||r->system_errno<0||!r->error)return -1;
 for(size_t i=0;i<sizeof(errors)/sizeof(errors[0]);i++)if(!strcmp(r->error,errors[i]))known=1;if(!known)return -1;
 char p[DN_JSON_CAP];size_t n=0;int pass=!strcmp(r->error,"none");
 if(pass&&(!r->address_count||!r->address_list_complete||r->gai_rc||r->system_errno||r->sid!=r->ppid||r->pgid!=r->ppid||r->foreground_pgid!=r->ppid||!r->entry_fd_checked||r->entry_fd_bound<3||!r->request_shutdown||!r->peer_eof||!r->socket_created||!r->socket_closed||r->request_bytes!=186||!r->response_bytes||!r->http.eof_observed||!r->http.framing_complete||!r->http.example_content))return -1;
 #define PUT(...) do{if(append(p,sizeof(p),&n,__VA_ARGS__))return -1;}while(0)
 PUT("{\"version\":1,\"profile\":\"%s\",\"nonce\":\"%s\",\"instance\":\"%s\",\"slot\":%u,\"pid\":%d,\"ppid\":%d,\"sid\":%d,\"pgid\":%d,\"foreground_pgid\":%d,\"status\":\"%s\",\"error\":\"%s\",\"error_errno\":%d,\"gai_rc\":%d,\"system_errno\":%d,\"address_list_complete\":%s,\"addresses\":[",DN_PROFILE,r->identity.nonce,r->identity.instance,r->identity.slot,r->pid,r->ppid,r->sid,r->pgid,r->foreground_pgid,pass?"PASS":"FAILED",r->error,r->error_errno,r->gai_rc,r->system_errno,r->address_list_complete?"true":"false");
 for(unsigned i=0;i<r->address_count;i++){if(i)PUT(",");if(ip(p,sizeof(p),&n,r->addresses[i]))return -1;}
 PUT("],\"selected\":");uint8_t zero[4]={0};if(ip(p,sizeof(p),&n,r->address_count?r->addresses[0]:zero))return -1;
 PUT(",\"local\":{\"address\":");if(ip(p,sizeof(p),&n,r->local))return -1;PUT(",\"port\":%u},\"peer\":{\"address\":",r->local_port);if(ip(p,sizeof(p),&n,r->peer))return -1;
 PUT(",\"port\":%u},\"entry_fd_check_observed\":%s,\"entry_fd_scan_bound_exclusive\":%u,\"tcp_maxseg_request\":256,\"tcp_maxseg_observed\":%d,\"request_hex\":\"",r->peer_port,r->entry_fd_checked?"true":"false",r->entry_fd_bound,r->tcp_maxseg_observed);
 if(hex(p,sizeof(p),&n,r->request,r->request_bytes))return -1;PUT("\",\"response_hex\":\"");if(hex(p,sizeof(p),&n,r->response,r->response_bytes))return -1;
 PUT("\",\"request_bytes\":%zu,\"response_bytes\":%zu,\"request_shutdown\":%s,\"peer_eof\":%s,\"socket_created\":%s,\"socket_closed\":%s,\"http\":{\"status\":%u,\"header_bytes\":%zu,\"body_bytes\":%zu,\"framing\":%u,\"framing_complete\":%s,\"eof_observed\":%s,\"example_content\":%s},\"started_ms\":%llu,\"elapsed_ms\":%llu}\n",r->request_bytes,r->response_bytes,r->request_shutdown?"true":"false",r->peer_eof?"true":"false",r->socket_created?"true":"false",r->socket_closed?"true":"false",r->http.example_content?200u:0u,r->http.header_bytes,r->http.body_bytes,(unsigned)r->http.framing,r->http.framing_complete?"true":"false",r->http.eof_observed?"true":"false",r->http.example_content?"true":"false",(unsigned long long)r->started_ms,(unsigned long long)r->elapsed_ms);
 if(n>=cap)return -1;memcpy(out,p,n+1);*length=n;return 0;
 #undef PUT
}
