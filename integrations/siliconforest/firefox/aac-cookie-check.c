/* SPDX-License-Identifier: MIT */
#include "aac-cookie.h"
#include <assert.h>
#include <stdio.h>
static void same(const uint8_t *data,size_t size){
 const uint8_t *config=NULL;size_t count=0;
 assert(sffx_aac_cookie(data,size,&config,&count));
 assert(count==2&&config[0]==0x11&&config[1]==0x90);
}
int main(void){
 const uint8_t raw[]={0x11,0x90};
 // ES_Descriptor -> DecoderConfigDescriptor -> DecoderSpecificInfo (AAC-LC).
 const uint8_t wrapped[]={0,0,0,0,3,22,0,1,0,4,17,0x40,0x15,
  0,0,0,0,0,0,0,0,0,0,0,5,2,0x11,0x90};
 same(raw,sizeof(raw));same(wrapped,sizeof(wrapped));same(wrapped+4,sizeof(wrapped)-4);
 const uint8_t *out=NULL;size_t count=0;
 for(size_t size=2;size<sizeof(wrapped)-4;size++)
  assert(!sffx_aac_cookie(wrapped+4,size,&out,&count));
 const uint8_t bad_length[]={3,255,255,255,255,0};
 assert(!sffx_aac_cookie(bad_length,sizeof(bad_length),&out,&count));
 uint8_t fuzz[64];uint32_t state=0x45879abdu;
 for(int n=0;n<20000;n++){
  size_t size=(unsigned)n%65;
  for(size_t i=0;i<size;i++){state=state*1664525u+1013904223u;fuzz[i]=(uint8_t)(state>>24);}
  if(sffx_aac_cookie(fuzz,size,&out,&count))
   assert(out>=fuzz&&out<=fuzz+size&&count>=2&&count<=(size_t)(fuzz+size-out));
 }
 puts("AAC cookie raw/ESDS/truncation/length/20000 bounded fuzz checks pass (host parser only)");
 return 0;
}
