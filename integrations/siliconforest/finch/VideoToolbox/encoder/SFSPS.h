/* SPDX-License-Identifier: MIT */
/* Bounded H.264 SPS classification; only proven frame-only coding is accepted.
 * ISO/IEC 14496-10 syntax facts, own implementation, no SDK declarations. */
#ifndef SF_VT_SPS_H
#define SF_VT_SPS_H
#include <stddef.h>
#include <stdint.h>
struct sf_sps_reader { const uint8_t *p; size_t n,i; unsigned bits,byte,zeros; int bad; };
static unsigned sf_sps_bit(struct sf_sps_reader *r) {
 if(!r->bits) {
  if(r->i>=r->n){r->bad=1;return 0;}
  unsigned b=r->p[r->i++];
  if(r->zeros>=2 && b==3){
   if(r->i>=r->n || r->p[r->i]>3){r->bad=1;return 0;}
   b=r->p[r->i++];r->zeros=0;
  }
  r->zeros=b==0?r->zeros+1:0;r->byte=b;r->bits=8;
 }
 return (r->byte>>--r->bits)&1;
}
static uint32_t sf_sps_bits(struct sf_sps_reader *r,unsigned n) {
 uint32_t v=0;while(n--)v=(v<<1)|sf_sps_bit(r);return v;
}
static uint32_t sf_sps_ue(struct sf_sps_reader *r) {
 unsigned zeros=0;while(!r->bad&&!sf_sps_bit(r)){if(++zeros>30){r->bad=1;return 0;}}
 return r->bad?0:((UINT32_C(1)<<zeros)-1)+sf_sps_bits(r,zeros);
}
static int32_t sf_sps_se(struct sf_sps_reader *r) {
 uint32_t v=sf_sps_ue(r);return (v&1)?(int32_t)((v+1)/2):-(int32_t)(v/2);
}
static int sf_vt_h264_progressive(const uint8_t *nal,size_t length) {
 if(!nal || length<5 || length>65536 || (nal[0]&0x9f)!=7)return 0;
 struct sf_sps_reader r={.p=nal+1,.n=length-1};
 unsigned profile=sf_sps_bits(&r,8);sf_sps_bits(&r,16);
 if(sf_sps_ue(&r)>31)return 0;
 if(profile==100||profile==110||profile==122||profile==244||profile==44||profile==83||profile==86||profile==118||profile==128||profile==138||profile==139||profile==134||profile==135) {
  unsigned chroma=sf_sps_ue(&r);if(chroma>3)return 0;
  if(chroma==3)sf_sps_bit(&r);
  if(sf_sps_ue(&r)>6||sf_sps_ue(&r)>6)return 0;
  sf_sps_bit(&r);
  if(sf_sps_bit(&r))for(unsigned i=0;i<(chroma==3?12:8)&&!r.bad;i++)if(sf_sps_bit(&r)) {
   int last=8,next=8;
   for(unsigned j=0;j<(i<6?16:64)&&!r.bad;j++) {
    if(next){int delta=sf_sps_se(&r);if(delta < -128 || delta>127)return 0;next=(last+delta+256)%256;}
    if(next)last=next;
   }
  }
 } else if(profile!=66 && profile!=77 && profile!=88)return 0;
 if(sf_sps_ue(&r)>12)return 0;
 unsigned order=sf_sps_ue(&r);if(order>2)return 0;
 if(!order){if(sf_sps_ue(&r)>12)return 0;}
 else if(order==1){
  sf_sps_bit(&r);sf_sps_se(&r);sf_sps_se(&r);unsigned count=sf_sps_ue(&r);if(count>255)return 0;
  for(unsigned i=0;i<count&&!r.bad;i++)sf_sps_se(&r);
 }
 if(sf_sps_ue(&r)>16)return 0;sf_sps_bit(&r);
 if(sf_sps_ue(&r)>511||sf_sps_ue(&r)>511)return 0;
 unsigned frame_only=sf_sps_bit(&r);return !r.bad && frame_only==1;
}
#endif
