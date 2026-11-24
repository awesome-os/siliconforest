/* Host fixtures only. Descriptor bytes here are not a device descriptor. */
#ifdef NDEBUG
#error M1TP_COLLECTOR_TEST_REQUIRES_ASSERTIONS
#endif
#include "trackpad_collector.h"
#include "observation_layout.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
static const char nonce[]="0123456789abcdef0123456789abcdef0123456789abcdef";
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void good(struct m1tp_observation *p)
{
 memset(p,0,sizeof(*p));p->version=1;p->result=0;p->stage=M1TP_OBS_COMPLETE;p->request_mask=31;
 p->metadata_payload_bytes=16;p->descriptor_payload_bytes=2;p->descriptor_bytes=2;
 p->metadata_header[1]=2;p->metadata_header[5]=33;p->metadata_header[6]=15;p->metadata_header[7]=2;
 p->metadata_header[10]=0xf6;p->metadata_header[11]=0x0f;
 p->descriptor[0]=0x85;p->descriptor[1]=2; /* arbitrary fixture, not HID-validity proof */
}
static void rejected(struct m1tp_observation *p,size_t n)
{
 struct tpec_summary out,before;memset(&out,0xa5,sizeof(out));before=out;
 assert(tpec_validate((const uint8_t*)p,n,&out)==-1);
 assert(memcmp(&out,&before,sizeof(out))==0);
}
static void states(void)
{
 struct m1tp_observation p;struct tpec_summary s;good(&p);
 assert(tpec_validate((const uint8_t*)&p,sizeof(p),&s)==0 && s.result==0 && s.descriptor_bytes==2);
 for(unsigned i=0;i<7;i++) {
  good(&p);uint8_t *b=(uint8_t*)&p;static const unsigned off[]={0,4,8,12,16,20,24};
  put32(b+off[i],0xffffffffu);
  rejected(&p,sizeof(p));
 }
 good(&p);p.descriptor[511]=1;rejected(&p,sizeof(p));
 good(&p);rejected(&p,555);rejected(&p,557);
 for(unsigned i=0;i<4;i++) {
  static const unsigned masks[]={0,1,3,7};memset(&p,0,sizeof(p));
  p.version=1;p.stage=0;p.result=1;p.request_mask=masks[i];
  assert(tpec_validate((const uint8_t*)&p,sizeof(p),&s)==0 && s.result==1);
 }
 good(&p);p.result=1;rejected(&p,sizeof(p));
 good(&p);p.request_mask=15;rejected(&p,sizeof(p));
 good(&p);p.descriptor_payload_bytes=0;p.descriptor_bytes=0;memset(p.descriptor,0,512);rejected(&p,sizeof(p));
 good(&p);p.metadata_header[1]=1;rejected(&p,sizeof(p));
 good(&p);p.metadata_header[6]=p.metadata_header[7]=0;rejected(&p,sizeof(p));
 good(&p);p.metadata_payload_bytes=15;rejected(&p,sizeof(p));
 /* Genuine source-shaped refusal states. No hardware is read. */
 good(&p);p.result=-1;p.stage=1;p.request_mask=15;p.metadata_payload_bytes=0;
 memset(p.metadata_header,0,16);p.descriptor_payload_bytes=0;p.descriptor_bytes=0;memset(p.descriptor,0,512);
 assert(tpec_validate((const uint8_t*)&p,sizeof(p),&s)==0);
 p.result=-2;p.stage=2;
 assert(tpec_validate((const uint8_t*)&p,sizeof(p),&s)==0);
 p.metadata_payload_bytes=7;p.metadata_header[1]=2;p.metadata_header[5]=33;
 assert(tpec_validate((const uint8_t*)&p,sizeof(p),&s)==0); /* genuine short header prefix */
 p.metadata_header[7]=1;rejected(&p,sizeof(p));p.metadata_header[7]=0;
 p.metadata_payload_bytes=16;p.metadata_header[1]=2;p.metadata_header[6]=10;
 rejected(&p,sizeof(p)); /* claimed invalid but valid metadata */
 p.stage=3;p.result=-3;p.request_mask=31;
 assert(tpec_validate((const uint8_t*)&p,sizeof(p),&s)==0);
 p.stage=4;p.result=-4;p.descriptor_payload_bytes=513;
 assert(tpec_validate((const uint8_t*)&p,sizeof(p),&s)==0);
 p.descriptor_payload_bytes=512;rejected(&p,sizeof(p));
}
struct fake {struct m1tp_observation p;int pg,ig,calls,mismatch;size_t len;};
static int get(void *v,enum tpec_copy which,uint8_t *buf,size_t cap,size_t *len,int32_t *detail)
{
 struct fake *f=v;f->calls++;*detail=0;assert(cap==4096);
 int code=which==TPEC_PROVIDER?f->pg:f->ig;if(code)return code;
 *len=f->len;
 if(*len<=sizeof(f->p)){memcpy(buf,&f->p,*len);if(which==TPEC_INSTANCE&&f->mismatch)buf[44]^=1;}
 return TPEC_GET_OK;
}
static int run(struct fake *f)
{
 char *args[]={"darwin-trackpad-enum","--nonce",(char*)nonce};FILE *out=tmpfile();assert(out);
 int rc=tpec_run(3,args,get,f,out);assert(fclose(out)==0);return rc;
}
static void flow(void)
{
 struct fake f;memset(&f,0,sizeof(f));good(&f.p);f.len=sizeof(f.p);
 assert(run(&f)==0 && f.calls==2);
 f.calls=0;f.ig=TPEC_GET_NO_ENTRY;assert(run(&f)==0 && f.calls==2);
 f.calls=0;f.pg=TPEC_GET_NO_PROPERTY;assert(run(&f)==1 && f.calls==1);
 f.pg=TPEC_GET_ERROR;f.calls=0;assert(run(&f)==1 && f.calls==1);
 f.pg=0;f.ig=0;f.mismatch=1;assert(run(&f)==1);
 f.mismatch=0;f.len=555;assert(run(&f)==1);
 f.len=4097;assert(run(&f)==1);
 memset(&f,0,sizeof(f));f.p.version=1;f.p.result=1;f.len=sizeof(f.p);f.ig=TPEC_GET_NO_ENTRY;
 assert(run(&f)==77);
 f.calls=0;char *args[]={"x"};FILE*out=tmpfile();assert(out);
 assert(tpec_run(1,args,get,&f,out)==2 && f.calls==0);assert(fclose(out)==0);
}
static int emit(int mode)
{
 struct fake f;memset(&f,0,sizeof(f));good(&f.p);f.len=sizeof(f.p);
 if(mode>=1&&mode<=5){
  f.p.result=mode==1?1:-(mode-1);f.p.stage=(uint32_t)(mode-1);
  f.p.descriptor_bytes=0;f.p.descriptor_payload_bytes=mode==5?513:0;memset(f.p.descriptor,0,512);
  if(mode<=3){
   f.p.request_mask=mode==1?7:15;
   f.p.metadata_payload_bytes=mode==3?7:0;memset(f.p.metadata_header,0,16);
   if(mode==3){f.p.metadata_header[1]=2;f.p.metadata_header[5]=33;}
  }
  f.ig=TPEC_GET_NO_ENTRY;
 }else if(mode==6)f.p.version=2;
 else if(mode==7)f.pg=TPEC_GET_NO_PROPERTY;
 else if(mode==8)f.mismatch=1;
 char *args[]={"darwin-trackpad-enum","--nonce",(char*)nonce};
 return tpec_run(3,args,get,&f,stdout);
}
int main(int argc,char **argv)
{
 if(argc==3 && !strcmp(argv[1],"--emit"))return emit(atoi(argv[2]));
 states();flow();
 assert(!tpec_nonce_valid("A123") && tpec_nonce_valid(nonce));
 puts("HOST_ONLY_TP_COLLECTOR states+flow PASS; hardware=none");
 return 0;
}
