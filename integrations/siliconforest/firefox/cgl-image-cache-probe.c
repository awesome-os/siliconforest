/* SPDX-License-Identifier: MIT
 * Real CGL/IOSurface cache admission, hit, active-image and teardown probe.
 * CPU readback verifies pixels; it is diagnostic, not transport performance.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef void *IOSurfaceRef,*CGLContextObj,*CGLPixelFormatObj;
typedef unsigned GLenum,GLuint;
typedef int GLint,GLsizei;
#define LOAD(lib,ret,name,args) ret (*name) args=dlsym(lib,#name); if(!name){fprintf(stderr,"missing %s\n",#name);return 2;}
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static IOSurfaceRef make_surface(IOSurfaceRef (*create)(CFDictionaryRef),int width,int height){
 CFMutableDictionaryRef p=CFDictionaryCreateMutable(0,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 const char *keys[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement","IOSurfacePixelFormat"};
 int values[]={width,height,4,0x42475241};
 for(int i=0;i<4;i++){CFStringRef k=CFStringCreateWithCString(0,keys[i],kCFStringEncodingUTF8);CFNumberRef n=CFNumberCreate(0,kCFNumberIntType,&values[i]);CFDictionarySetValue(p,k,n);CFRelease(k);CFRelease(n);}
 CFDictionarySetValue(p,CFSTR("IOSurfaceIsGlobal"),kCFBooleanTrue);IOSurfaceRef s=create(p);CFRelease(p);return s;
}
int main(int argc,char **argv){
 setvbuf(stdout,0,_IOLBF,0);REQUIRE(argc==2);
 REQUIRE(getenv("SFFX_GPU_IMAGE_CACHE")&&!strcmp(getenv("SFFX_GPU_IMAGE_CACHE"),"1"));
 REQUIRE(getenv("SFFX_IOSURFACE_GPU")&&!strcmp(getenv("SFFX_IOSURFACE_GPU"),"1"));
 void *surface=dlopen("/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface",RTLD_NOW|RTLD_GLOBAL);
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib||!surface){fprintf(stderr,"dlopen %s\n",dlerror());return 2;}
 LOAD(surface,IOSurfaceRef,IOSurfaceCreate,(CFDictionaryRef));LOAD(surface,IOSurfaceRef,IOSurfaceLookup,(uint32_t));LOAD(surface,uint32_t,IOSurfaceGetID,(IOSurfaceRef));
 LOAD(surface,int,IOSurfaceSFGPUCopyFD,(IOSurfaceRef));
 LOAD(lib,int,CGLChoosePixelFormat,(const int *,CGLPixelFormatObj *,int *));LOAD(lib,int,CGLCreateContext,(CGLPixelFormatObj,CGLContextObj,CGLContextObj *));
 LOAD(lib,int,CGLSetCurrentContext,(CGLContextObj));LOAD(lib,int,CGLDestroyContext,(CGLContextObj));LOAD(lib,int,CGLDestroyPixelFormat,(CGLPixelFormatObj));
 LOAD(lib,int,CGLTexImageIOSurface2D,(CGLContextObj,GLenum,GLenum,GLsizei,GLsizei,GLenum,GLenum,IOSurfaceRef,GLuint));
 LOAD(lib,int,CGLSFFXCopyFramebufferRect,(IOSurfaceRef,int,int,int,int,int));
 LOAD(lib,void,CGLSFFXImageCacheStats,(uint64_t *));LOAD(lib,void,CGLSFFXImageCacheFootprint,(uint64_t *));
 LOAD(lib,void,CGLSFFXTransferStats,(uint64_t *));
 LOAD(lib,void,glGenTextures,(GLsizei,GLuint *));LOAD(lib,void,glBindTexture,(GLenum,GLuint));LOAD(lib,void,glDeleteTextures,(GLsizei,const GLuint *));
 LOAD(lib,void,glTexParameteri,(GLenum,GLenum,GLint));LOAD(lib,void,glGenFramebuffers,(GLsizei,GLuint *));LOAD(lib,void,glBindFramebuffer,(GLenum,GLuint));
 LOAD(lib,void,glFramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint));LOAD(lib,GLenum,glCheckFramebufferStatus,(GLenum));LOAD(lib,void,glDeleteFramebuffers,(GLsizei,const GLuint *));
 LOAD(lib,void,glClearColor,(float,float,float,float));LOAD(lib,void,glClear,(unsigned));LOAD(lib,void,glReadPixels,(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *));
 LOAD(lib,void,glFinish,(void));LOAD(lib,GLenum,glGetError,(void));LOAD(lib,const char *,glGetString,(GLenum));
 int attributes[]={99,0x3200,73,96,0},count=0;CGLPixelFormatObj pf=0;CGLContextObj ctx=0;
 REQUIRE(!CGLChoosePixelFormat(attributes,&pf,&count)&&!CGLCreateContext(pf,0,&ctx)&&!CGLSetCurrentContext(ctx));
 REQUIRE(glGetString(0x1f01)&&strstr(glGetString(0x1f01),"Apple M1"));
 GLuint fbo=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(0x8d40,fbo);
 IOSurfaceRef tiny=make_surface(IOSurfaceCreate,32,32);REQUIRE(tiny);
 int fd=IOSurfaceSFGPUCopyFD(tiny);REQUIRE(fd>=0);close(fd);
 REQUIRE(!(IOSurfaceGetID(tiny)&0x80000000u));
 uint64_t transfer_before[4],transfer_after[4];CGLSFFXTransferStats(transfer_before);
 uint64_t before[3],after[3],foot[4];CGLSFFXImageCacheStats(before);
 for(int round=0;round<2;round++){
  IOSurfaceRef alias=round?IOSurfaceLookup(IOSurfaceGetID(tiny)):tiny;REQUIRE(alias);
  GLuint texture;glGenTextures(1,&texture);glBindTexture(0x0de1,texture);glTexParameteri(0x0de1,0x2801,0x2600);glTexParameteri(0x0de1,0x2800,0x2600);
  REQUIRE(!CGLTexImageIOSurface2D(ctx,0x0de1,0x1908,32,32,0x80e1,0x8367,alias,0));
  glFramebufferTexture2D(0x8d40,0x8ce0,0x0de1,texture,0);REQUIRE(glCheckFramebufferStatus(0x8d40)==0x8cd5);
  if(!round){glClearColor(0,1,0,1);glClear(0x4000);glFinish();}
  unsigned char pixel[4]={0};glReadPixels(0,0,1,1,0x1908,0x1401,pixel);REQUIRE(!memcmp(pixel,(unsigned char[]){0,255,0,255},4));
  glFramebufferTexture2D(0x8d40,0x8ce0,0x0de1,0,0);glDeleteTextures(1,&texture);if(round)CFRelease(alias);
 }
 CGLSFFXImageCacheStats(after);
 printf("FXIMAGECACHE admission hits=%llu imports=%llu let_go=%llu\n",(unsigned long long)(after[0]-before[0]),(unsigned long long)(after[1]-before[1]),(unsigned long long)(after[2]-before[2]));
 REQUIRE(after[0]>before[0]);
 REQUIRE(!CGLSFFXCopyFramebufferRect(tiny,0,INT_MAX,INT_MAX,INT_MAX,INT_MAX));
 REQUIRE(!CGLSFFXCopyFramebufferRect(tiny,0,0,0,INT_MAX,1));
 REQUIRE(!CGLSFFXCopyFramebufferRect(tiny,0,-1,0,1,1));
 CFRelease(tiny);
 // Keep more active texture storage than the cache budget. Uncached images
 // must still work, and eviction must never invalidate an active image.
 GLuint textures[40]={0};IOSurfaceRef surfaces[40]={0};uint64_t maximum=0;
 for(int i=0;i<40;i++){
  surfaces[i]=make_surface(IOSurfaceCreate,2048,512);REQUIRE(surfaces[i]);
  glGenTextures(1,&textures[i]);glBindTexture(0x0de1,textures[i]);glTexParameteri(0x0de1,0x2801,0x2600);glTexParameteri(0x0de1,0x2800,0x2600);
  REQUIRE(!CGLTexImageIOSurface2D(ctx,0x0de1,0x1908,2048,512,0x80e1,0x8367,surfaces[i],0));
  glFramebufferTexture2D(0x8d40,0x8ce0,0x0de1,textures[i],0);REQUIRE(glCheckFramebufferStatus(0x8d40)==0x8cd5);
  glClearColor(i&1,0,!(i&1),1);glClear(0x4000);glFinish();
  CGLSFFXImageCacheFootprint(foot);REQUIRE(foot[0]<=96&&foot[1]<=foot[3]&&foot[2]<=foot[1]);if(foot[1]>maximum)maximum=foot[1];
 }
 for(int i=0;i<40;i++){
  glFramebufferTexture2D(0x8d40,0x8ce0,0x0de1,textures[i],0);unsigned char pixel[4]={0};
  glReadPixels(0,0,1,1,0x1908,0x1401,pixel);REQUIRE(pixel[0]==((i&1)?255:0)&&pixel[1]==0&&pixel[2]==((i&1)?0:255)&&pixel[3]==255);
 }
 REQUIRE(!glGetError());glFramebufferTexture2D(0x8d40,0x8ce0,0x0de1,0,0);glDeleteTextures(40,textures);
 CGLSFFXTransferStats(transfer_after);
 REQUIRE(transfer_after[0]==transfer_before[0]&&transfer_after[1]==transfer_before[1]&&transfer_after[3]>transfer_before[3]);
 for(int i=0;i<40;i++)CFRelease(surfaces[i]);glDeleteFramebuffers(1,&fbo);
 CGLSFFXImageCacheFootprint(foot);uint64_t unused=foot[2];
 REQUIRE(!CGLSetCurrentContext(0)&&!CGLDestroyContext(ctx)&&!CGLDestroyPixelFormat(pf));
 CGLSFFXImageCacheFootprint(foot);REQUIRE(foot[0]==0&&foot[1]==0&&foot[2]==0);
 printf("FXIMAGECACHE pass=1 hits=%llu max_layout_bytes=%llu unused_before_context_teardown=%llu after_count=%llu after_bytes=%llu active_pixels=40 rect_overflow_refused=3\n",(unsigned long long)(after[0]-before[0]),(unsigned long long)maximum,(unsigned long long)unused,(unsigned long long)foot[0],(unsigned long long)foot[1]);
 return 0;
}
