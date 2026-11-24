#include "probe_record.h"
#include <string.h>
struct reader { const uint8_t *p;size_t n; };
static int text(struct reader *r,const char *s) {
 size_t n=strlen(s);if(n>r->n||memcmp(r->p,s,n))return 0;
 r->p+=n;r->n-=n;return 1;
}
static int decimal(struct reader *r,uint32_t high,uint32_t *value) {
 if(!r->n||r->p[0]<'0'||r->p[0]>'9')return 0;
 int zero=r->p[0]=='0';uint32_t n=0;unsigned count=0;
 while(r->n&&r->p[0]>='0'&&r->p[0]<='9') {
  unsigned d=r->p[0]-'0';if((zero&&count)||d>high||n>(high-d)/10)return 0;
  n=n*10+d;r->p++;r->n--;count++;
 }
 *value=n;return 1;
}
int dlr_probe_parse(const uint8_t *p,size_t n,const char nonce[49],int32_t pid,struct dlr_probe_record *out) {
 if(!p||!n||n>DLR_PROBE_CAP||!nonce||!out||pid<2||strlen(nonce)!=48)return 0;
 for(unsigned i=0;i<48;i++)if(!((nonce[i]>='0'&&nonce[i]<='9')||(nonce[i]>='a'&&nonce[i]<='f')))return 0;
 struct reader r={p,n};struct dlr_probe_record result;memset(&result,0,sizeof(result));uint32_t actual;
 if(!text(&r,"{\"version\":1,\"nonce\":\"")||!text(&r,nonce)||
    !text(&r,"\",\"name\":\"example.com\",\"pid\":")||!decimal(&r,INT32_MAX,&actual)||actual!=(uint32_t)pid||
    !text(&r,",\"status\":\"PASS\",\"gai_rc\":0,\"system_errno\":0,\"error\":\"none\",\"address_list_complete\":true,\"addresses\":["))return 0;
 result.pid=pid;
 for(;;) {
  uint32_t ip=0;
  if(result.count==DLR_ADDRESS_CAP||!text(&r,"\""))return 0;
  for(unsigned i=0;i<4;i++) {uint32_t octet;if(!decimal(&r,255,&octet))return 0;ip=(ip<<8)|octet;if(i<3&&!text(&r,"."))return 0;}
  if(!text(&r,"\""))return 0;
  result.ipv4[result.count++]=ip;
  if(r.n&&r.p[0]==','){r.p++;r.n--;continue;}
  break;
 }
 if(!text(&r,"]}\n")||r.n)return 0;
 *out=result;return 1;
}
