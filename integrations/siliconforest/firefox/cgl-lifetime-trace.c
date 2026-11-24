/* SPDX-License-Identifier: MIT
 * Transparent counts of real context ownership calls from QuartzCore.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
extern int CGLCreateContext(void *,void *,void **);
extern int CGLDestroyContext(void *);
static int (*real_create)(void *,void *,void **);
static int (*real_destroy)(void *);
static uint64_t counts[2];
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void resolve(void){
 real_create=dlsym(RTLD_NEXT,"CGLCreateContext");
 real_destroy=dlsym(RTLD_NEXT,"CGLDestroyContext");
 Dl_info provider={0};if(real_destroy)dladdr((void *)real_destroy,&provider);
 fprintf(stderr,"FXCGLLIFETIME provider=%s create=%d destroy=%d\n",provider.dli_fname?provider.dli_fname:"missing",real_create!=0,real_destroy!=0);
}
static int create(void *format,void *shared,void **result){
 pthread_once(&once,resolve);int status=real_create?real_create(format,shared,result):10004;
 if(!status)__atomic_fetch_add(&counts[0],1,__ATOMIC_RELAXED);return status;
}
static int destroy(void *context){
 pthread_once(&once,resolve);int status=real_destroy?real_destroy(context):10004;
 if(!status)__atomic_fetch_add(&counts[1],1,__ATOMIC_RELAXED);return status;
}
void SFFXCGLLifetimeStats(uint64_t values[2]){
 for(int i=0;i<2;i++)values[i]=__atomic_load_n(&counts[i],__ATOMIC_RELAXED);
}
__attribute__((used,section("__DATA,__interpose")))
static const struct{const void *replacement,*original;} interpose[]={
 {(const void *)create,(const void *)CGLCreateContext},
 {(const void *)destroy,(const void *)CGLDestroyContext}
};
