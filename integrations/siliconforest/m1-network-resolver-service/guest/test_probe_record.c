#ifdef NDEBUG
#error assertions must be active
#endif
#include "probe_record.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char nonce[]="0123456789abcdef0123456789abcdef0123456789abcdef";
int main(void) {
 char line[8193],mutant[8193];struct dlr_probe_record out,unchanged;memset(&out,0xa5,sizeof(out));unchanged=out;
 int n=snprintf(line,sizeof(line),"{\"version\":1,\"nonce\":\"%s\",\"name\":\"example.com\",\"pid\":15,\"status\":\"PASS\",\"gai_rc\":0,\"system_errno\":0,\"error\":\"none\",\"address_list_complete\":true,\"addresses\":[\"104.20.23.154\",\"172.66.147.243\",\"104.20.23.154\"]}\n",nonce);
 assert(n>0);assert(dlr_probe_parse((const uint8_t *)line,(size_t)n,nonce,15,&out));assert(out.count==3&&out.ipv4[0]==0x6814179a&&out.ipv4[1]==0xac4293f3&&out.ipv4[2]==out.ipv4[0]);
 unsigned cases=1;
 for(int i=0;i<n;i++){out=unchanged;assert(!dlr_probe_parse((uint8_t *)line,(size_t)i,nonce,15,&out));assert(!memcmp(&out,&unchanged,sizeof(out)));cases++;}
 for(int i=0;i<n;i++){memcpy(mutant,line,(size_t)n);mutant[i]='\0';out=unchanged;assert(!dlr_probe_parse((uint8_t *)mutant,(size_t)n,nonce,15,&out));assert(!memcmp(&out,&unchanged,sizeof(out)));cases++;}
 assert(!dlr_probe_parse((uint8_t *)line,(size_t)n,nonce,16,&out));
 memcpy(mutant,line,(size_t)n);mutant[n]='\n';assert(!dlr_probe_parse((uint8_t *)mutant,(size_t)n+1,nonce,15,&out));
 const char *bad[]={"01.2.3.4","256.2.3.4","1.2.3","1.2.3.4.5","-1.2.3.4","1.2.3.4\",\""};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {char *a=strstr(line,"104.20.23.154");size_t off=(size_t)(a-line);memcpy(mutant,line,off);int k=snprintf(mutant+off,sizeof(mutant)-off,"%s\"]}\n",bad[i]);assert(k>0);assert(!dlr_probe_parse((uint8_t *)mutant,off+(size_t)k,nonce,15,&out));cases++;}
 char *start=strstr(line,"104.20.23.154");size_t base=(size_t)(start-line)-1;
 for(unsigned count=0;count<=33;count++) {
  memcpy(mutant,line,base);size_t used=base;
  for(unsigned i=0;i<count;i++){int k=snprintf(mutant+used,sizeof(mutant)-used,"%s\"1.2.3.4\"",i?",":"");assert(k>0);used+=(size_t)k;}
  memcpy(mutant+used,"]}\n",3);used+=3;
  out=unchanged;int accepted=dlr_probe_parse((uint8_t *)mutant,used,nonce,15,&out);assert(accepted==(count>=1&&count<=32));
  if(!accepted)assert(!memcmp(&out,&unchanged,sizeof(out)));else assert(out.count==count);cases++;
 }
 printf("probe_record: %u strict grammar, prefix and preservation cases passed\n",cases+2);return 0;
}
