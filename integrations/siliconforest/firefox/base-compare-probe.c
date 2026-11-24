/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gpu-present.h"
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){
 unsigned char *a=malloc(8*1024*1024+64),*b=malloc(8*1024*1024+64);if(!a||!b)return 2;
 unsigned random=0x12345678;for(size_t i=0;i<8*1024*1024+64;i++){random=random*1664525+1013904223;a[i]=b[i]=random>>24;}
 size_t lengths[]={0,1,15,16,17,63,64,65,127,128,1024,8193};unsigned checks=0,errors=0;
 for(int offset=0;offset<16;offset++)for(unsigned n=0;n<sizeof(lengths)/sizeof(*lengths);n++){
  size_t size=lengths[n];checks++;errors+=!sfGPUBaseEqual(a+offset,b+offset,size);
  if(!size)continue;
  size_t positions[]={0,size/2,size-1};
  for(int p=0;p<3;p++){b[offset+positions[p]]^=0x81;checks++;errors+=sfGPUBaseEqual(a+offset,b+offset,size);b[offset+positions[p]]^=0x81;}
 }
 int (*volatile compare)(const void *,const void *,size_t)=sfGPUBaseEqual;
 volatile unsigned equal=0;double begin=seconds();for(int repeat=0;repeat<16;repeat++)equal+=compare(a,b,8*1024*1024);
 double wall=seconds()-begin;
 printf("FXBASECOMPARE vector=%d checks=%u errors=%u bytes=%u iterations=16 equal=%u wall=%.6f\n",getenv("SFFX_GPU_VECTOR_COMPARE")!=NULL,checks,errors,8*1024*1024,equal,wall);
 free(a);free(b);return errors||equal!=16?1:0;
}
