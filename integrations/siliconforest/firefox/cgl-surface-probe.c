/* SPDX-License-Identifier: MIT
 * Two exec-separated CGL contexts use the same IOSurface. Frame transport is
 * tested separately from CPU/GL verification; bridge counters expose fallback.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <spawn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
typedef void *IOSurfaceRef;
extern IOSurfaceRef IOSurfaceCreate(CFDictionaryRef),IOSurfaceLookup(uint32_t);
extern uint32_t IOSurfaceGetID(IOSurfaceRef);
extern void *IOSurfaceGetBaseAddress(IOSurfaceRef);
extern size_t IOSurfaceGetBytesPerRow(IOSurfaceRef);
extern char **environ;
typedef unsigned GLenum,GLuint;
typedef int GLint,GLsizei;
typedef void *CGLContextObj,*CGLPixelFormatObj;
#define LOAD(ret,name,args) ret (*name) args=dlsym(lib,#name); if(!name){fprintf(stderr,"missing %s\n",#name);return 2;}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);
 int child=argc==6&&!strcmp(argv[1],"child"),selected=argc==5&&!strcmp(argv[1],"case");
 unsigned target=child?(unsigned)strtoul(argv[3],0,0):0;
 int profile=child?(int)strtol(argv[4],0,0):0,require=child?atoi(argv[5]):(argc==2&&!strcmp(argv[1],"--require-gpu"));
 const char *library=getenv("SFFX_CGL_PROBE_LIBRARY");
 void *lib=dlopen(library&&*library?library:"/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);
 if(!lib){fprintf(stderr,"CGL: %s\n",dlerror());return 2;}
 LOAD(int,CGLChoosePixelFormat,(const int *,CGLPixelFormatObj *,int *));
 LOAD(int,CGLCreateContext,(CGLPixelFormatObj,CGLContextObj,CGLContextObj *));
 LOAD(int,CGLSetCurrentContext,(CGLContextObj));
 LOAD(int,CGLDestroyContext,(CGLContextObj));
 LOAD(int,CGLDestroyPixelFormat,(CGLPixelFormatObj));
 LOAD(int,CGLTexImageIOSurface2D,(CGLContextObj,GLenum,GLenum,GLsizei,GLsizei,GLenum,GLenum,IOSurfaceRef,GLuint));
 LOAD(void,glGenTextures,(GLsizei,GLuint *));LOAD(void,glBindTexture,(GLenum,GLuint));
 LOAD(void,glTexParameteri,(GLenum,GLenum,GLint));LOAD(void,glDeleteTextures,(GLsizei,const GLuint *));
 LOAD(void,glGenFramebuffers,(GLsizei,GLuint *));LOAD(void,glBindFramebuffer,(GLenum,GLuint));
 LOAD(void,glFramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint));
 LOAD(GLenum,glCheckFramebufferStatus,(GLenum));LOAD(void,glDeleteFramebuffers,(GLsizei,const GLuint *));
 LOAD(void,glClearColor,(float,float,float,float));LOAD(void,glClear,(unsigned));
 LOAD(void,glReadPixels,(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *));
 LOAD(void,glFinish,(void));LOAD(GLenum,glGetError,(void));LOAD(const char *,glGetString,(GLenum));
 void (*stats)(uint64_t[4])=dlsym(lib,"CGLSFFXTransferStats");
 int failures=0;
 if(selected){target=(unsigned)strtoul(argv[2],0,0);profile=(int)strtol(argv[3],0,0);require=atoi(argv[4]);}
 for(int p=0;p<((child||selected)?1:2);p++)for(int t=0;t<((child||selected)?1:2);t++){
  if(!child&&!selected){profile=p?0x4100:0x3200;target=t?0x84f5:0x0de1;}
  int attributes[]={99,profile,73,96,0},count=0;CGLPixelFormatObj pf=0;CGLContextObj ctx=0;
  int status=CGLChoosePixelFormat(attributes,&pf,&count);
  if(!status)status=CGLCreateContext(pf,0,&ctx);if(!status)status=CGLSetCurrentContext(ctx);
  if(status||!ctx){printf("FXCGLSURFACE context_error=%d\n",status);return 3;}
  int hardware=glGetString(0x1f01)&&strstr(glGetString(0x1f01),"Apple M1")!=0;
  IOSurfaceRef surface=0;
  if(child)surface=IOSurfaceLookup((uint32_t)strtoul(argv[2],0,10));
  else{
   CFMutableDictionaryRef props=CFDictionaryCreateMutable(0,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
   const char *keys[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement","IOSurfacePixelFormat"};int values[]={32,32,4,0x42475241};
   for(int i=0;i<4;i++){CFStringRef key=CFStringCreateWithCString(0,keys[i],kCFStringEncodingUTF8);CFNumberRef n=CFNumberCreate(0,kCFNumberIntType,&values[i]);CFDictionarySetValue(props,key,n);CFRelease(n);CFRelease(key);}
   CFDictionarySetValue(props,CFSTR("IOSurfaceIsGlobal"),kCFBooleanTrue);surface=IOSurfaceCreate(props);CFRelease(props);
  }
  if(!surface){printf("FXCGLSURFACE surface_missing child=%d\n",child);return 4;}
  GLuint texture=0,fbo=0;uint64_t before[4]={0},after[4]={0};if(stats)stats(before);
  glGenTextures(1,&texture);glBindTexture(target,texture);glTexParameteri(target,0x2801,0x2600);glTexParameteri(target,0x2800,0x2600);
  status=CGLTexImageIOSurface2D(ctx,target,0x1908,32,32,0x80e1,0x8367,surface,0);
  glGenFramebuffers(1,&fbo);glBindFramebuffer(0x8d40,fbo);glFramebufferTexture2D(0x8d40,0x8ce0,target,texture,0);
  unsigned complete=glCheckFramebufferStatus(0x8d40);
  if(!child){glClearColor(0,1,0,1);glClear(0x4000);glFinish();}
  unsigned char pixels[4096]={0};glReadPixels(0,0,32,32,0x1908,0x1401,pixels);
  int mismatch=0;for(int i=0;i<1024;i++)mismatch+=memcmp(pixels+i*4,(unsigned char[]){0,255,0,255},4)!=0;
  int cpuMismatch=0;unsigned char *base=IOSurfaceGetBaseAddress(surface);size_t stride=IOSurfaceGetBytesPerRow(surface);
  for(int y=0;y<32;y++)for(int x=0;x<32;x++)cpuMismatch+=memcmp(base+y*stride+x*4,(unsigned char[]){0,255,0,255},4)!=0;
  int childStatus=0;
  if(!child){
   char id[32],tg[24],pr[24],rq[8];snprintf(id,sizeof(id),"%u",IOSurfaceGetID(surface));snprintf(tg,sizeof(tg),"%u",target);snprintf(pr,sizeof(pr),"%d",profile);snprintf(rq,sizeof(rq),"%d",require);
   char *args[]={argv[0],"child",id,tg,pr,rq,0};pid_t pid=0;int error=posix_spawn(&pid,argv[0],0,0,args,environ),reaped=0;
   if(error)childStatus=error;else{for(int i=0;i<200;i++){if(waitpid(pid,&childStatus,WNOHANG)==pid){reaped=1;break;}usleep(50000);}if(!reaped){kill(pid,SIGKILL);for(int i=0;i<20;i++){if(waitpid(pid,&childStatus,WNOHANG)==pid){reaped=1;break;}usleep(50000);}printf("FXCGLSURFACE child_timeout pid=%d reaped=%d\n",pid,reaped);childStatus=255;}}
  }
  if(stats)stats(after);unsigned error=glGetError();int transfer=1;
  if(require)transfer=stats&&after[0]==before[0]&&after[1]==before[1]&&((target==0x0de1&&(!child?after[3]>before[3]:1))||(target==0x84f5&&after[2]>before[2]));
  int pass=!status&&complete==0x8cd5&&!mismatch&&!cpuMismatch&&!childStatus&&!error&&hardware&&transfer;
  printf("FXCGLSURFACE child=%d profile=%x target=%x bind=%d fbo=%x gl_mismatch=%d cpu_mismatch=%d child_status=%d gl_error=%x hardware=%d cpu_upload=%llu cpu_readback=%llu gpu_blit=%llu direct_commit=%llu transfer_pass=%d pass=%d\n",child,profile,target,status,complete,mismatch,cpuMismatch,childStatus,error,hardware,(unsigned long long)(after[0]-before[0]),(unsigned long long)(after[1]-before[1]),(unsigned long long)(after[2]-before[2]),(unsigned long long)(after[3]-before[3]),transfer,pass);
  failures+=!pass;glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);CFRelease(surface);CGLSetCurrentContext(0);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);
 }
 printf("FXCGLSURFACE failures=%d\n",failures);return failures?1:0;
}
