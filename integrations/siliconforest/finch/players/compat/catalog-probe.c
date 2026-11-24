/* SPDX-License-Identifier: MIT */
/* Own declarations/public ABI probe, touches only our fresh temporary file. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
int main(void){
 void *h=dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices",RTLD_NOW);
 if(!h){fprintf(stderr,"catalog provider missing\n");return 1;}
 int32_t(*make)(const uint8_t*,void*,uint8_t*)=dlsym(h,"FSPathMakeRef");
 int32_t(*catalog)(const void*,uint32_t,void*,void*,void*,void*)=dlsym(h,"FSGetCatalogInfo");
 int32_t(*volume)(int16_t,void*,uint32_t)=dlsym(h,"FSGetVolumeParms");
 if(!make||!catalog||!volume){puts("CATALOG missing API");return 1;}
 char file[]="sf-catalog.XXXXXX",path[4096];int fd=mkstemp(file);if(fd<0)return 2;
 if(write(fd,"our-file",8)!=8)return 3;close(fd);if(!realpath(file,path))return 4;
 uint8_t ref[80]={0},info[1024]={0},parms[512]={0};
 int32_t a=make((uint8_t*)path,ref,NULL),b=a?a:catalog(ref,4,info,NULL,NULL,NULL);
 int16_t v=0;memcpy(&v,info+2,2);int32_t c=b?b:volume(v,parms,sizeof(parms));
 int16_t version=0;memcpy(&version,parms,2);
 printf("CATALOG make=%d catalog=%d volume-nonzero=%d parms=%d version=%d\n",a,b,v!=0,c,version);
 for(unsigned off=24;!c&&off<=40;off+=4){
  uintptr_t value=0;memcpy(&value,parms+off,sizeof(value));if(!value)continue;
  char name[64]={0};mach_vm_size_t got=0;
  kern_return_t result=mach_vm_read_overwrite(mach_task_self(),value,63,(mach_vm_address_t)name,&got);
  if(result==KERN_SUCCESS&&got&&(!strncmp(name,"disk",4)||!strncmp(name,"/dev/disk",9)))
   printf("CATALOG device-pointer-offset=%u local-disk-name=1\n",off);
 }
 unlink(file);return a||b||c?1:0;
}
