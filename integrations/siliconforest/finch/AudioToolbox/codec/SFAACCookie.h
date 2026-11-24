/* SPDX-License-Identifier: MIT
 * Extract MPEG-4 DecoderSpecificInfo from an ESDS AAC magic cookie. FFmpeg
 * expects AudioSpecificConfig; callers may also supply that raw form directly.
 */
#ifndef SF_AAC_COOKIE_H
#define SF_AAC_COOKIE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static int sf_aac_descriptors(const uint8_t *p,size_t size,int depth,
                              const uint8_t **config,size_t *config_size){
 if(depth>3)return 0;
 while(size){
  if(size<2)return 0;
  unsigned tag=*p++;size--;size_t length=0;int complete=0;
  for(int i=0;i<4&&size;i++){
   unsigned byte=*p++;size--;length=(length<<7)|(byte&127);
   if(!(byte&128)){complete=1;break;}
  }
  if(!complete||length>size)return 0;
  const uint8_t *body=p;size_t count=length;
  if(tag==5){
   if(count<2)return 0;
   *config=body;*config_size=count;return 1;
  }
  if(tag==3){
   if(count<3)return 0;
   unsigned flags=body[2];body+=3;count-=3;
   if(flags&128){if(count<2)return 0;body+=2;count-=2;}
   if(flags&64){
    if(!count)return 0;
    size_t url=*body++;count--;if(url>count)return 0;body+=url;count-=url;
   }
   if(flags&32){if(count<2)return 0;body+=2;count-=2;}
   return sf_aac_descriptors(body,count,depth+1,config,config_size);
  }
  if(tag==4){
   if(count<13||body[0]!=0x40||(body[1]>>2)!=5)return 0;
   return sf_aac_descriptors(body+13,count-13,depth+1,config,config_size);
  }
  p+=length;size-=length;
 }
 return 0;
}
static int sf_aac_cookie(const uint8_t *p,size_t size,
                          const uint8_t **config,size_t *config_size){
 if(!p||size<2||!config||!config_size)return 0;
 // An optional esds atom header, then its version/flags full-box prefix.
 if(size>=8&&!memcmp(p+4,"esds",4)){
  uint32_t atom=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
  if(atom<12||atom>size)return 0;
  p+=8;size=atom-8;
 }
 if(size>=4&&!p[0]&&!p[1]&&!p[2]&&!p[3]){p+=4;size-=4;}
 if(size&&(*p==3||*p==4))return sf_aac_descriptors(p,size,0,config,config_size);
 // Raw ASC is validated by the real AAC decoder, without rewriting its bytes.
 if(size<2)return 0;
 *config=p;*config_size=size;return 1;
}
#endif
