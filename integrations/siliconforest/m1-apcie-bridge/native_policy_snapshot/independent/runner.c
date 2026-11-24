/* Original host-only independent driver; actual production decoder linked. */
#include "../policy_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error independent checks require assertions
#endif
static struct nps_workspace work;
static struct nps_result out,before;
static struct nps_expected expected,saved_expected;
static unsigned char raw[20000],saved[20000];
static unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static void span(struct mcp_range r){printf("[%llu,%llu]",(unsigned long long)r.base,(unsigned long long)r.bytes);}
static void aliases(size_t n){
 unsigned char *a=calloc(1,65536),*old=malloc(sizeof out);assert(a&&old);
 for(unsigned i=0;i<16;i++){
  memset(a,0,65536);memcpy(a,raw,n);memcpy(a+12000,&expected,sizeof expected);
  unsigned char *rp=a;struct nps_expected *ep=(void*)(a+12000);
  struct nps_workspace *wp=(void*)(a+13000);struct nps_result *op=(void*)(a+40000);
  switch(i){case 0:ep=(void*)rp;break;case 1:wp=(void*)rp;break;case 2:op=(void*)rp;break;
   case 3:wp=(void*)ep;break;case 4:op=(void*)ep;break;case 5:op=(void*)wp;break;
   case 6:ep=(void*)(rp+8);break;case 7:wp=(void*)(rp+8);break;case 8:op=(void*)(rp+8);break;
   case 9:wp=(void*)((unsigned char*)ep+8);break;case 10:op=(void*)((unsigned char*)ep+8);break;case 11:op=(void*)((unsigned char*)wp+8);break;
   case 12:rp=NULL;break;case 13:ep=NULL;break;case 14:wp=NULL;break;case 15:op=NULL;break;}
  if(op)memcpy(old,op,sizeof out);
  assert(nps_decode(rp,n,ep,wp,op)==NPS_ARGUMENT);
  if(op)assert(!memcmp(old,op,sizeof out));
 }
 free(old);free(a);
}
int main(void){
 unsigned char header[4];unsigned count=0;
 while(fread(header,1,4,stdin)==4){
  size_t n=u32(header);assert(n<=sizeof raw);
  assert(fread(&expected,1,sizeof expected,stdin)==sizeof expected);
  assert(fread(raw,1,n,stdin)==n);memcpy(saved,raw,n);saved_expected=expected;
  if(!count)aliases(n);
  memset(&out,0xa5,sizeof out);memcpy(&before,&out,sizeof out);memset(&work,0x73,sizeof work);
  int rc=nps_decode(raw,n,&expected,&work,&out);
  assert(!memcmp(raw,saved,n)&&!memcmp(&expected,&saved_expected,sizeof expected));
  if(rc){assert(!memcmp(&before,&out,sizeof out));printf("{\"rc\":%d}\n",rc);}
  else {
   assert(out.version==1&&out.board_profile==1&&out.page_shift==14&&out.pa_bits==36&&out.wire_profile==1&&out.coverage==1);
   assert(!memcmp(&out.identity,&expected,sizeof expected));assert(!memcmp(out.raw_mcc,raw+784,384));
   printf("{\"rc\":0,\"ranges\":[");span(out.dram);putchar(',');span(out.envelope);putchar(',');span(out.retained_boot);printf("],\"ram\":[");for(unsigned i=0;i<out.ram_count;i++){if(i)putchar(',');span(out.ram[i]);}
   printf("],\"union\":[");for(unsigned i=0;i<out.union_count;i++){if(i)putchar(',');span(out.relevant_union[i]);}
   printf("],\"records\":[");for(unsigned i=0;i<out.record_count;i++){
    struct nps_record *r=&out.record[i];if(i)putchar(',');printf("[%u,%u,%u,%u,%llu,%llu]",r->tag,r->ordinal,r->flags,r->auxiliary,(unsigned long long)r->range.base,(unsigned long long)r->range.bytes);}
   printf("],\"sources\":[");for(unsigned i=0;i<6;i++){
    assert(!memcmp(out.source[i].declared_source_sha256,raw+504+i*48,32));
    if(i)putchar(',');printf("[%u,%u,%u]",out.source[i].tag,out.source[i].status,out.source[i].count);}
   printf("],\"mcc\":[");unsigned first=1;for(unsigned i=0;i<4;i++)if(out.mcc.slot[i].enabled){
    if(!first)putchar(',');first=0;printf("[%u,%llu,%llu]",i,(unsigned long long)out.mcc.slot[i].range.base,(unsigned long long)out.mcc.slot[i].range.bytes);}
   printf("]}\n");
  }
  count++;
 }
 assert(feof(stdin));return 0;
}
