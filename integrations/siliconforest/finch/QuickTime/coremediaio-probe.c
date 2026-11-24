/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <string.h>
typedef struct {uint32_t selector,scope,element;} Address;
int main(void){struct stat st;if(!getenv("SF_QT_PD_ONLY")||stat("/dev/sfavd",&st))return 77;
 void*h=dlopen("/System/Library/Frameworks/CoreMediaIO.framework/Versions/A/CoreMediaIO",RTLD_NOW|RTLD_GLOBAL);
 if(!h){fprintf(stderr,"%s\n",dlerror());return 1;}
 int32_t(*get)(uint32_t,const Address*,uint32_t,const void*,uint32_t,uint32_t*,void*)=dlsym(h,"CMIOObjectGetPropertyData");
 uint32_t used=17;uint8_t data[32],expected[32];memset(data,0x5a,sizeof(data));memcpy(expected,data,sizeof(data));Address a={0};
 if(!get||get(1,&a,0,NULL,sizeof(data),&used,data)==0||used!=0||memcmp(data,expected,sizeof(data)))return 1;
 if(get(0,NULL,0,NULL,0,NULL,NULL)==0)return 1;
 puts("COREMEDIAIO unavailable=1 used=0 output-unchanged=1 null-query-failed=1");return 0;}
