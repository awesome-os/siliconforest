/* SPDX-License-Identifier: MIT
 * Two sharing contexts own the SAME numeric framebuffer name, with distinct
 * shared rectangle textures. A draw in A must publish A's IOSurface, not B's.
 * CPU fixture initialization and readback verify transport; they are not it.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef void *IOSurfaceRef;
extern IOSurfaceRef IOSurfaceCreate(CFDictionaryRef);
extern void *IOSurfaceGetBaseAddress(IOSurfaceRef);
extern size_t IOSurfaceGetBytesPerRow(IOSurfaceRef);
extern uint32_t IOSurfaceGetSeed(IOSurfaceRef);
extern int IOSurfaceLock(IOSurfaceRef,uint32_t,uint32_t *),IOSurfaceUnlock(IOSurfaceRef,uint32_t,uint32_t *);
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FXFBO failure_line=%d\n",__LINE__);return 2;} }while(0)
#define LOAD(ret,n,args) ret (*n)args=dlsym(lib,#n);REQUIRE(n)
static IOSurfaceRef make(void) {
 CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 const char *names[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement","IOSurfacePixelFormat"};int values[]={16,16,4,0x42475241};
 for(int i=0;i<4;i++){CFStringRef k=CFStringCreateWithCString(NULL,names[i],kCFStringEncodingUTF8);CFNumberRef v=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);CFDictionarySetValue(d,k,v);CFRelease(k);CFRelease(v);}
 IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);
 if(s){IOSurfaceLock(s,0,NULL);memset(IOSurfaceGetBaseAddress(s),0,IOSurfaceGetBytesPerRow(s)*16);IOSurfaceUnlock(s,0,NULL);}
 return s;
}
int main(int argc,char **argv) {
 setvbuf(stdout,NULL,_IOLBF,0);alarm(15);
 void *lib=dlopen(argc>1?argv[1]:"/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);
 LOAD(int,CGLChoosePixelFormat,(const int *,void **,int *));LOAD(int,CGLCreateContext,(void *,void *,void **));
 LOAD(int,CGLSetCurrentContext,(void *));LOAD(int,CGLDestroyContext,(void *));LOAD(int,CGLDestroyPixelFormat,(void *));
 LOAD(int,CGLTexImageIOSurface2D,(void *,unsigned,unsigned,int,int,unsigned,unsigned,void *,unsigned));
 LOAD(void,glGenTextures,(int,unsigned *));LOAD(void,glBindTexture,(unsigned,unsigned));LOAD(void,glTexParameteri,(unsigned,unsigned,int));
 LOAD(void,glDeleteTextures,(int,const unsigned *));LOAD(void,glGenFramebuffers,(int,unsigned *));LOAD(void,glBindFramebuffer,(unsigned,unsigned));
 LOAD(void,glFramebufferTexture2D,(unsigned,unsigned,unsigned,unsigned,int));LOAD(unsigned,glCheckFramebufferStatus,(unsigned));
 LOAD(void,glDeleteFramebuffers,(int,const unsigned *));LOAD(void,glClearColor,(float,float,float,float));LOAD(void,glClear,(unsigned));
 LOAD(void,glFinish,(void));LOAD(unsigned,glGetError,(void));LOAD(const char *,glGetString,(unsigned));
 LOAD(unsigned char,glIsTexture,(unsigned));LOAD(unsigned char,glIsFramebuffer,(unsigned));
 int failures=0;
 const int profiles[]={0x3200,0x4100};const unsigned targets[]={0x84f5,0x0de1};
 for(int profile=0;profile<2;profile++)for(int target=0;target<2;target++){
  int attrs[]={99,profiles[profile],73,96,0},count=0;void *pf=NULL,*ctx[2]={NULL,NULL};
  REQUIRE(!CGLChoosePixelFormat(attrs,&pf,&count));REQUIRE(!CGLCreateContext(pf,NULL,&ctx[0])&&!CGLCreateContext(pf,ctx[0],&ctx[1]));
  IOSurfaceRef surfaces[2]={make(),make()};REQUIRE(surfaces[0]&&surfaces[1]);
  unsigned textures[2]={0,0},fbos[2][16]={{0}};
  for(int i=0;i<2;i++){
   REQUIRE(!CGLSetCurrentContext(ctx[i]));REQUIRE(glGetString(0x1f01)&&strstr(glGetString(0x1f01),"Apple M1"));
   if(i)printf("FXFBO sharing texture_a_visible=%d fbo_a_visible=%d version=%s\n",glIsTexture(textures[0]),glIsFramebuffer(fbos[0][0]),glGetString(0x1f02));
   /* Allocate FBO names BEFORE import's temporary FBOs, preserving a collision. */
   glGenFramebuffers(16,fbos[i]);glGenTextures(1,&textures[i]);glBindTexture(targets[target],textures[i]);
   glTexParameteri(targets[target],0x2801,0x2600);glTexParameteri(targets[target],0x2800,0x2600);
   REQUIRE(!CGLTexImageIOSurface2D(ctx[i],targets[target],0x1908,16,16,0x80e1,0x8367,surfaces[i],0));
   glBindFramebuffer(0x8d40,fbos[i][0]);glFramebufferTexture2D(0x8d40,0x8ce0,targets[target],textures[i],0);
   REQUIRE(glCheckFramebufferStatus(0x8d40)==0x8cd5&&glGetError()==0);
  }
  printf("FXFBO names context_a=%p context_b=%p fbo_a=%u fbo_b=%u texture_a=%u texture_b=%u\n",ctx[0],ctx[1],fbos[0][0],fbos[1][0],textures[0],textures[1]);
  if(fbos[0][0]!=fbos[1][0]||textures[0]==textures[1]){
   fprintf(stderr,"FXFBO collision_not_established=1; no pixel regression verdict\n");return 77;
  }
  uint32_t before[2]={IOSurfaceGetSeed(surfaces[0]),IOSurfaceGetSeed(surfaces[1])};
  REQUIRE(!CGLSetCurrentContext(ctx[0]));glBindFramebuffer(0x8d40,fbos[0][0]);glClearColor(1,0,0,1);glClear(0x4000);glFinish();
  unsigned mismatch=0;IOSurfaceLock(surfaces[0],1,NULL);unsigned char *base=IOSurfaceGetBaseAddress(surfaces[0]);size_t stride=IOSurfaceGetBytesPerRow(surfaces[0]);
  for(int y=0;y<16;y++)for(int x=0;x<16;x++)mismatch+=memcmp(base+y*stride+x*4,(unsigned char[]){0,0,255,255},4)!=0;
  IOSurfaceUnlock(surfaces[0],1,NULL);
  uint32_t after[2]={IOSurfaceGetSeed(surfaces[0]),IOSurfaceGetSeed(surfaces[1])};
  unsigned error=glGetError();int pass=!mismatch&&!error&&after[0]!=before[0]&&after[1]==before[1];
  printf("FXFBO profile=%x target=%x same_fbo=%u texture_a=%u texture_b=%u mismatch=%u seed_a=%u->%u seed_b=%u->%u gl_error=%x pass=%d\n",profiles[profile],targets[target],fbos[0][0],textures[0],textures[1],mismatch,before[0],after[0],before[1],after[1],error,pass);
  failures+=!pass;
  /* Destruction of B's container objects must not remove A's same-name FBO.
   * Shared textures intentionally survive B until A explicitly deletes them. */
  REQUIRE(!CGLSetCurrentContext(NULL)&&!CGLDestroyContext(ctx[1]));ctx[1]=NULL;
  REQUIRE(!CGLSetCurrentContext(ctx[0]));
  before[0]=IOSurfaceGetSeed(surfaces[0]);before[1]=IOSurfaceGetSeed(surfaces[1]);
  glBindFramebuffer(0x8d40,fbos[0][0]);glClearColor(0,0,1,1);glClear(0x4000);glFinish();
  mismatch=0;IOSurfaceLock(surfaces[0],1,NULL);base=IOSurfaceGetBaseAddress(surfaces[0]);
  for(int y=0;y<16;y++)for(int x=0;x<16;x++)mismatch+=memcmp(base+y*stride+x*4,(unsigned char[]){255,0,0,255},4)!=0;
  IOSurfaceUnlock(surfaces[0],1,NULL);error=glGetError();
  after[0]=IOSurfaceGetSeed(surfaces[0]);after[1]=IOSurfaceGetSeed(surfaces[1]);
  pass=!mismatch&&!error&&after[0]!=before[0]&&after[1]==before[1];failures+=!pass;
  printf("FXFBO phase=after_peer_context_death profile=%x target=%x mismatch=%u seed_a=%u->%u seed_b=%u->%u gl_error=%x pass=%d\n",profiles[profile],targets[target],mismatch,before[0],after[0],before[1],after[1],error,pass);
  glDeleteFramebuffers(16,fbos[0]);glDeleteTextures(2,textures);
  CGLSetCurrentContext(NULL);CGLDestroyContext(ctx[0]);CFRelease(surfaces[0]);CFRelease(surfaces[1]);
  CGLDestroyPixelFormat(pf);
 }
 printf("FXFBO cases=4 failures=%d\n",failures);return failures?1:0;
}
