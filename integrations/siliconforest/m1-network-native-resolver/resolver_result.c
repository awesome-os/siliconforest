/* Original pure result/address validation; host tests never call a resolver. */
#include "resolver_result.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
int rr_nonce(const char *p,size_t n) {
 if(!p||n!=48)return 0;
 for(size_t i=0;i<n;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;
 return 1;
}
int rr_address(struct rr_result *r,int32_t family,int32_t socktype,int32_t protocol,
               const uint8_t *p,size_t n) {
 if(!r||r->error!=RR_NONE)return -1;
 if(r->count>=RR_ADDRESS_CAP){r->error=RR_CAP;return -1;}
 if(!p||family!=2||socktype!=1||protocol!=6||n!=16||p[0]!=16||p[1]!=2||p[2]||p[3]!=80){r->error=RR_SHAPE;return -1;}
 for(size_t i=8;i<16;i++)if(p[i]){r->error=RR_SHAPE;return -1;}
 memcpy(r->addresses[r->count++].bytes,p+4,4);return 0;
}
static int append(char *out,size_t cap,size_t *used,const char *format,...) {
 if(*used>=cap)return -1;
 va_list a;va_start(a,format);int n=vsnprintf(out+*used,cap-*used,format,a);va_end(a);
 if(n<0||(size_t)n>=cap-*used)return -1;
 *used+=(size_t)n;return 0;
}
int rr_format(const struct rr_result *r,const char *nonce,char *out,size_t cap,size_t *length) {
 static const char *const errors[]={"none","gai_error","empty_result","address_shape","address_cap","address_cycle","unexpected_failure_result"};
 if(!r||!nonce||!out||!length||!rr_nonce(nonce,strlen(nonce))||r->pid<2||r->count>RR_ADDRESS_CAP||
    r->error<0||r->error>=RR_ERROR_COUNT||r->system_errno<0||
    (r->error==RR_NONE&&(!r->count||r->gai_rc||r->system_errno))||
    (r->error==RR_GAI&&!r->gai_rc))return -1;
 char buffer[RR_OUTPUT_CAP];size_t used=0;
 if(append(buffer,sizeof(buffer),&used,"{\"version\":1,\"nonce\":\"%s\",\"name\":\"example.com\",\"pid\":%d,\"status\":\"%s\",\"gai_rc\":%d,\"system_errno\":%d,\"error\":\"%s\",\"address_list_complete\":%s,\"addresses\":[",nonce,r->pid,r->error==RR_NONE?"PASS":"FAILED",r->gai_rc,r->system_errno,errors[r->error],r->error==RR_NONE?"true":"false"))return -1;
 for(size_t i=0;i<r->count;i++){
  const uint8_t *p=r->addresses[i].bytes;
  if(append(buffer,sizeof(buffer),&used,"%s\"%u.%u.%u.%u\"",i?",":"",(unsigned)p[0],(unsigned)p[1],(unsigned)p[2],(unsigned)p[3]))return -1;
 }
 if(append(buffer,sizeof(buffer),&used,"]}\n")||used>=cap)return -1;
 memcpy(out,buffer,used+1);*length=used;return 0;
}
