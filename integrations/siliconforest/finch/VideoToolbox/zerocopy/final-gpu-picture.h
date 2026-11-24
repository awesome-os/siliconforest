/* SPDX-License-Identifier: MIT */
/* Native correctness control using FX's existing NV12 RECT blit/completion
 * lease, not another CGL provider or direct-alias implementation. Import both
 * planes before VT invalidation, then verify the independent GL texture after
 * releasing the CV picture. The explicit verification readback is not playback
 * performance or zero-total-copy evidence. No screen or input is used. */
/* Native test only: use FX's existing CGL NV12 completion lease, then verify
 * independent GL texture bytes after VT invalidation and CV picture release.
 * Explicit readback is verification overhead, never a playback-speed claim.
 * Function declarations below are ours; no Apple SDK headers are used. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static struct {
 void *library,*format,*context;
 unsigned textures[2];
 size_t width,height;
 uint64_t before[4];
 int (*choose)(const int *,void **,int *);
 int (*create)(void *,void *,void **);
 int (*current)(void *);
 int (*destroy)(void *);
 int (*destroyFormat)(void *);
 int (*plane)(void *,unsigned,unsigned,int,int,unsigned,unsigned,IOSurfaceRef,unsigned);
 void (*gen)(int,unsigned *);
 void (*bind)(unsigned,unsigned);
 void (*delete)(int,const unsigned *);
 void (*pack)(unsigned,int);
 void (*read)(unsigned,int,unsigned,unsigned,void *);
 unsigned (*error)(void);
 const char *(*string)(unsigned);
 void (*stats)(uint64_t *);
} sfFinalGPU;
static void sfFinalGPUClose(void){
 if(sfFinalGPU.context){
  sfFinalGPU.current(sfFinalGPU.context);
  sfFinalGPU.delete(2,sfFinalGPU.textures);
  sfFinalGPU.current(NULL);sfFinalGPU.destroy(sfFinalGPU.context);
 }
 if(sfFinalGPU.format)sfFinalGPU.destroyFormat(sfFinalGPU.format);
 if(sfFinalGPU.library)dlclose(sfFinalGPU.library);
 memset(&sfFinalGPU,0,sizeof(sfFinalGPU));
}
static int sfFinalGPUPrepare(CVPixelBufferRef image){
 sfFinalGPU.library=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);
 if(!sfFinalGPU.library)return -1;
#define SF_LOAD(field,name) do{*(void **)(&sfFinalGPU.field)=dlsym(sfFinalGPU.library,name);if(!sfFinalGPU.field)return -1;}while(0)
 SF_LOAD(choose,"CGLChoosePixelFormat");SF_LOAD(create,"CGLCreateContext");
 SF_LOAD(current,"CGLSetCurrentContext");SF_LOAD(destroy,"CGLDestroyContext");
 SF_LOAD(destroyFormat,"CGLDestroyPixelFormat");SF_LOAD(plane,"CGLTexImageIOSurface2D");
 SF_LOAD(gen,"glGenTextures");SF_LOAD(bind,"glBindTexture");SF_LOAD(delete,"glDeleteTextures");
 SF_LOAD(pack,"glPixelStorei");SF_LOAD(read,"glGetTexImage");
 SF_LOAD(error,"glGetError");SF_LOAD(string,"glGetString");SF_LOAD(stats,"CGLSFFXTransferStats");
#undef SF_LOAD
 Dl_info provider={0};const char *expected=getenv("SF_CAPTURE_EXPECT_GL");
 if(!expected||!dladdr((const void *)sfFinalGPU.plane,&provider)||!provider.dli_fname||
    strcmp(provider.dli_fname,expected))return -1;
 printf("CAPTURE_GPU_PROVIDER %s\n",provider.dli_fname);
 int attributes[]={99,0x3200,73,96,0},count=0;
 if(sfFinalGPU.choose(attributes,&sfFinalGPU.format,&count)||!count||
    sfFinalGPU.create(sfFinalGPU.format,NULL,&sfFinalGPU.context)||
    sfFinalGPU.current(sfFinalGPU.context))return -1;
 const char *renderer=sfFinalGPU.string(0x1f01);
 if(!renderer||!strstr(renderer,"Apple M1"))return -1;
 sfFinalGPU.width=CVPixelBufferGetWidth(image);sfFinalGPU.height=CVPixelBufferGetHeight(image);
 IOSurfaceRef storage=CVPixelBufferGetIOSurface(image);
 if(!storage||!sfFinalGPU.width||!sfFinalGPU.height||sfFinalGPU.width>8192||sfFinalGPU.height>8192)return -1;
 sfFinalGPU.stats(sfFinalGPU.before);sfFinalGPU.gen(2,sfFinalGPU.textures);
 for(unsigned p=0;p<2;p++){
  unsigned format=p?0x8227:0x1903;
  int width=(int)(p?(sfFinalGPU.width+1)/2:sfFinalGPU.width);
  int height=(int)(p?(sfFinalGPU.height+1)/2:sfFinalGPU.height);
  sfFinalGPU.bind(0x84f5,sfFinalGPU.textures[p]);
  /* The qualified CGL NV12 path holds storage through its real glFinish.
   * Direct aliasing without that contract is refused by the provider. */
  if(sfFinalGPU.plane(sfFinalGPU.context,0x84f5,format,width,height,format,0x1401,storage,p)||
     sfFinalGPU.error())return -1;
 }
 return 0;
}
static int sfFinalGPUVerify(const char *expected){
 struct sf_md5 md5;sf_md5_init(&md5);
 sfFinalGPU.pack(0x0d05,1);
 for(unsigned p=0;p<2;p++){
  size_t width=p?(sfFinalGPU.width+1)/2:sfFinalGPU.width;
  size_t height=p?(sfFinalGPU.height+1)/2:sfFinalGPU.height;
  size_t bytes=width*height*(p?2:1);
  unsigned char *pixels=calloc(1,bytes);if(!pixels)return -1;
  sfFinalGPU.bind(0x84f5,sfFinalGPU.textures[p]);
  sfFinalGPU.read(0x84f5,0,p?0x8227:0x1903,0x1401,pixels);
  unsigned error=sfFinalGPU.error();
  if(!error)sf_md5_update(&md5,pixels,bytes);
  free(pixels);if(error)return -1;
 }
 char actual[33];sf_md5_final(&md5,actual);
 uint64_t after[4]={0};sfFinalGPU.stats(after);
 if(strcmp(actual,expected)||after[0]!=sfFinalGPU.before[0]||after[1]!=sfFinalGPU.before[1]||
    after[2]<=sfFinalGPU.before[2])return -1;
 printf("CAPTURE_GPU_FINAL hash=%s cpu-upload=%llu cpu-readback=%llu gpu-blit-bytes=%llu after-invalidate-and-picture-release=1 verification-readback=1\n",
  actual,(unsigned long long)(after[0]-sfFinalGPU.before[0]),
  (unsigned long long)(after[1]-sfFinalGPU.before[1]),
  (unsigned long long)(after[2]-sfFinalGPU.before[2]));
 sfFinalGPUClose();return 0;
}
