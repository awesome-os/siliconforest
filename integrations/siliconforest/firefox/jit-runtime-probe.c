/* SPDX-License-Identifier: MIT
 * Private, explicitly signed JIT execution control. Report unsupported thread
 * write protection honestly; execution is not proof of W^X enforcement.
 */
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifndef MAP_JIT
#define MAP_JIT 0x800
#endif
static uint64_t nanos(struct timespec t){return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
int main(int argc,char **argv){
 int rx=argc==2&&!strcmp(argv[1],"--rx");
 int (*supported)(void)=dlsym(RTLD_DEFAULT,"pthread_jit_write_protect_supported_np");
 void (*protect)(int)=dlsym(RTLD_DEFAULT,"pthread_jit_write_protect_np");
 void (*invalidate)(void *,size_t)=dlsym(RTLD_DEFAULT,"sys_icache_invalidate");
 int (*csops)(pid_t,unsigned,void *,size_t)=dlsym(RTLD_DEFAULT,"csops");
 if(!supported||!protect||!invalidate){fprintf(stderr,"FXJIT required_native_API_absent\n");return 2;}
 uint32_t flags=0;int csstatus=csops?csops(getpid(),0,&flags,sizeof(flags)):-1;
 int thread_protection=supported();size_t size=(size_t)getpagesize();
 int permissions=rx?(PROT_READ|PROT_WRITE):(PROT_READ|PROT_WRITE|PROT_EXEC);
 void *memory=mmap(NULL,size,permissions,MAP_PRIVATE|MAP_ANON|(rx?0:MAP_JIT),-1,0);
 if(memory==MAP_FAILED){printf("FXJIT mode=%s mmap_errno=%d thread_write_protect_supported=%d cs_status=%d flags=%x pass=0\n",rx?"rw-to-rx":"MAP_JIT",errno,thread_protection,csstatus,flags);return 1;}
 const uint32_t code[]={0x52800540,0xd65f03c0}; // mov w0,#42; ret
 if(!rx)protect(0);
 memcpy(memory,code,sizeof(code));invalidate(memory,sizeof(code));
 if(rx){if(mprotect(memory,size,PROT_READ|PROT_EXEC)){printf("FXJIT mprotect_errno=%d pass=0\n",errno);munmap(memory,size);return 1;}}
 else protect(1);
 int value=((int (*)(void))memory)();
 struct timespec resolution,prior,current;int clock_status=clock_getres(CLOCK_MONOTONIC,&resolution);
 uint64_t step=UINT64_MAX;clock_gettime(CLOCK_MONOTONIC,&prior);
 for(unsigned i=0;i<10000;i++){clock_gettime(CLOCK_MONOTONIC,&current);uint64_t delta=nanos(current)-nanos(prior);if(delta&&delta<step)step=delta;prior=current;}
 printf("FXJIT mode=%s result=%d thread_write_protect_supported=%d enforcement_claim=0 cs_status=%d flags=%x page_size=%zu clock_status=%d clock_resolution_ns=%llu observed_min_step_ns=%llu pass=%d\n",rx?"rw-to-rx":"MAP_JIT",value,thread_protection,csstatus,flags,size,clock_status,(unsigned long long)(clock_status?0:nanos(resolution)),(unsigned long long)(step==UINT64_MAX?0:step),value==42);
 munmap(memory,size);return value==42?0:1;
}
