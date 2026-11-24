/* Real host sanitizer execution of pure classifiers, never getaddrinfo/network. */
#include "resolver_result.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { checks++;assert(x); } while(0)
static const char nonce[]="0123456789abcdef0123456789abcdef0123456789abcdef";
static void init(struct rr_result *r){memset(r,0,sizeof(*r));r->pid=14;}
int main(void){
 struct rr_result r;uint8_t sa[16]={16,2,0,80,104,20,23,154};char out[RR_OUTPUT_CAP];size_t n=0;
 CHECK(rr_nonce(nonce,48));CHECK(!rr_nonce(nonce,47));CHECK(!rr_nonce(NULL,48));
 for(unsigned i=0;i<48;i++){char bad[49];memcpy(bad,nonce,sizeof(bad));bad[i]='G';CHECK(!rr_nonce(bad,48));}
 init(&r);CHECK(!rr_address(&r,2,1,6,sa,16));CHECK(!rr_address(&r,2,1,6,sa,16));
 CHECK(r.count==2&&!memcmp(r.addresses[0].bytes,r.addresses[1].bytes,4));
 CHECK(!rr_format(&r,nonce,out,sizeof(out),&n));CHECK(out[n-1]=='\n'&&out[n]==0&&n<1024);
 CHECK(strstr(out,"\"addresses\":[\"104.20.23.154\",\"104.20.23.154\"]")!=NULL);
 CHECK(strstr(out,"\"address_list_complete\":true")!=NULL);CHECK(strstr(out,"\"status\":\"PASS\"")!=NULL);
 for(unsigned i=0;i<16;i++)if(i<4||i>=8)for(unsigned bit=0;bit<8;bit++){
  struct rr_result bad;init(&bad);uint8_t changed[16];memcpy(changed,sa,16);changed[i]^=(uint8_t)(1u<<bit);
  CHECK(rr_address(&bad,2,1,6,changed,16));CHECK(bad.error==RR_SHAPE&&bad.count==0);
 }
 for(size_t size=0;size<32;size++)if(size!=16){init(&r);CHECK(rr_address(&r,2,1,6,sa,size));CHECK(r.error==RR_SHAPE);}
 for(int field=0;field<3;field++)for(int value=0;value<33;value++)if(value!=(field==0?2:field==1?1:6)){
  init(&r);CHECK(rr_address(&r,field==0?value:2,field==1?value:1,field==2?value:6,sa,16));CHECK(r.error==RR_SHAPE);
 }
 init(&r);for(size_t i=0;i<RR_ADDRESS_CAP;i++){sa[7]=(uint8_t)i;CHECK(!rr_address(&r,2,1,6,sa,16));}
 CHECK(r.count==RR_ADDRESS_CAP);CHECK(!rr_format(&r,nonce,out,sizeof(out),&n));
 CHECK(rr_address(&r,2,1,6,sa,16));CHECK(r.error==RR_CAP&&r.count==RR_ADDRESS_CAP);
 CHECK(!rr_format(&r,nonce,out,sizeof(out),&n));CHECK(strstr(out,"\"address_list_complete\":false")!=NULL);
 CHECK(strstr(out,"\"status\":\"FAILED\"")!=NULL);
 init(&r);r.error=RR_GAI;r.gai_rc=-3;r.system_errno=5;CHECK(!rr_format(&r,nonce,out,sizeof(out),&n));
 CHECK(strstr(out,"\"gai_rc\":-3")!=NULL);CHECK(strstr(out,"\"system_errno\":5")!=NULL);
 for(int error=RR_EMPTY;error<RR_ERROR_COUNT;error++){init(&r);r.error=(enum rr_error)error;CHECK(!rr_format(&r,nonce,out,sizeof(out),&n));}
 init(&r);CHECK(rr_format(&r,nonce,out,sizeof(out),&n));r.error=RR_GAI;CHECK(rr_format(&r,nonce,out,sizeof(out),&n));
 r.gai_rc=1;r.pid=1;CHECK(rr_format(&r,nonce,out,sizeof(out),&n));r.pid=14;r.system_errno=-1;CHECK(rr_format(&r,nonce,out,sizeof(out),&n));
 init(&r);CHECK(!rr_address(&r,2,1,6,sa,16));CHECK(!rr_format(&r,nonce,out,sizeof(out),&n));
 for(size_t cap=0;cap<=n;cap++){char small[RR_OUTPUT_CAP];memset(small,'x',sizeof(small));size_t length=9999;
  CHECK(rr_format(&r,nonce,small,cap,&length));CHECK(length==9999&&small[0]=='x');}
 CHECK(!rr_format(&r,nonce,out,n+1,&n));
 printf("Pure resolver address/result classifiers: %u sanitizer assertions PASS; no DNS execution\n",checks);
 return 0;
}
