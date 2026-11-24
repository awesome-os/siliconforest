/* SPDX-License-Identifier: MIT
 * Private diagnostic: observe the real public converter ABI without changing it.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { double rate; uint32_t id,flags,bytesPacket,framesPacket,bytesFrame,channels,bits,reserved; } ASBD;
extern int32_t AudioConverterNew(const ASBD *,const ASBD *,void **);
static int32_t (*real_new)(const ASBD *,const ASBD *,void **);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void resolve(void){ real_new=dlsym(RTLD_NEXT,"AudioConverterNew"); }
static void format(const char *name,const ASBD *a){
 if(a)fprintf(stderr,"FXAUDIO %s rate=%g id=%08x flags=%x bytesPacket=%u framesPacket=%u bytesFrame=%u channels=%u bits=%u\n",
  name,a->rate,a->id,a->flags,a->bytesPacket,a->framesPacket,a->bytesFrame,a->channels,a->bits);
}
static int32_t traced_new(const ASBD *in,const ASBD *out,void **result){
 pthread_once(&once,resolve);
 Dl_info info={0};if(real_new)dladdr((void *)real_new,&info);
 format("input",in);format("output",out);
 int32_t status=real_new?real_new(in,out,result):-4;
 fprintf(stderr,"FXAUDIO AudioConverterNew provider=%s status=%d\n",info.dli_fname?info.dli_fname:"missing",status);
 return status;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct{const void *replacement,*original;} interpose={
 (const void *)traced_new,(const void *)AudioConverterNew
};
