/* SPDX-License-Identifier: MIT
 * Exec-separated NV12 plane import and completion-bound reuse control.
 * Synthetic CPU fixture writes and GL readback are verification only. Transport
 * through CGL must report zero CPU upload/readback and real GPU plane blits.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <spawn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
typedef void *IOSurfaceRef;
extern IOSurfaceRef IOSurfaceCreate(CFDictionaryRef),IOSurfaceLookup(uint32_t);
extern uint32_t IOSurfaceGetID(IOSurfaceRef);
extern size_t IOSurfaceGetPlaneCount(IOSurfaceRef),IOSurfaceGetBytesPerRowOfPlane(IOSurfaceRef,size_t);
extern void *IOSurfaceGetBaseAddressOfPlane(IOSurfaceRef,size_t);
extern void IOSurfaceIncrementUseCount(IOSurfaceRef),IOSurfaceDecrementUseCount(IOSurfaceRef);
extern int32_t IOSurfaceGetUseCount(IOSurfaceRef);
extern int IOSurfaceIsInUse(IOSurfaceRef),IOSurfaceLock(IOSurfaceRef,uint32_t,uint32_t *),IOSurfaceUnlock(IOSurfaceRef,uint32_t,uint32_t *);
extern char **environ;
#define W 65
#define H 33
#define STRIDE 128
#define UVOFF (STRIDE*48)
#define ALLOC 16384
#define REQUIRE(v) do{if(!(v)){fprintf(stderr,"FXNV12 failure_line=%d\n",__LINE__);return 1;}}while(0)
#define LOAD(ret,name,args) ret (*name)args=dlsym(lib,#name);REQUIRE(name)
static int width(int p){return p?(W+1)/2:W;}
static int height(int p){return p?(H+1)/2:H;}
static unsigned char sample(int p,int x,int y,int channel,int revision){return (unsigned char)(p?(x*11+y*5+channel*71+revision*31):(x*13+y*7+revision*19));}
static void number(CFMutableDictionaryRef p,const char *name,int64_t value){
 CFStringRef key=CFStringCreateWithCString(NULL,name,kCFStringEncodingUTF8);CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&value);
 CFDictionarySetValue(p,key,n);CFRelease(n);CFRelease(key);
}
static IOSurfaceRef make(uint32_t format,int bad,int layout){
 CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 number(d,"IOSurfaceWidth",W);number(d,"IOSurfaceHeight",H);number(d,"IOSurfaceBytesPerRow",layout?192:STRIDE);number(d,"IOSurfaceBytesPerElement",1);
 number(d,"IOSurfacePixelFormat",format);number(d,"IOSurfaceAllocSize",ALLOC);CFDictionarySetValue(d,CFSTR("IOSurfaceIsGlobal"),kCFBooleanTrue);
 CFMutableArrayRef planes=CFArrayCreateMutable(NULL,0,&kCFTypeArrayCallBacks);
 for(int p=0;p<2;p++){
  CFMutableDictionaryRef q=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  number(q,"IOSurfacePlaneWidth",width(p));number(q,"IOSurfacePlaneHeight",height(p));
  number(q,"IOSurfacePlaneBytesPerRow",bad==2&&p?1:STRIDE);number(q,"IOSurfacePlaneBytesPerElement",p?2:1);
  number(q,"IOSurfacePlaneOffset",p?(bad==1?0:bad==4?512+STRIDE*(H+7)-1:UVOFF):(layout?512:0));
  number(q,"IOSurfacePlaneSize",bad==3&&p?1:STRIDE*(height(p)+(layout?7:0)));
  CFArrayAppendValue(planes,q);CFRelease(q);
 }
 CFDictionarySetValue(d,CFSTR("IOSurfacePlaneInfo"),planes);CFRelease(planes);
 IOSurfaceRef surface=IOSurfaceCreate(d);CFRelease(d);return surface;
}
static void fill(IOSurfaceRef s,int revision){
 IOSurfaceLock(s,0,NULL);
 for(int p=0;p<2;p++){
  unsigned char *data=IOSurfaceGetBaseAddressOfPlane(s,p);size_t stride=IOSurfaceGetBytesPerRowOfPlane(s,p);
  memset(data,0xa5,stride*height(p));
  for(int y=0;y<height(p);y++)for(int x=0;x<width(p);x++)for(int c=0;c<(p?2:1);c++)data[y*stride+x*(p?2:1)+c]=sample(p,x,y,c,revision);
 }
 IOSurfaceUnlock(s,0,NULL);
}
static int child(int fd,uint32_t id,int profile){
 alarm(20);IOSurfaceRef s=IOSurfaceLookup(id);REQUIRE(s&&IOSurfaceGetPlaneCount(s)==2&&IOSurfaceGetUseCount(s)==0&&IOSurfaceIsInUse(s));
 const char *library=getenv("SFFX_CGL_PROBE_LIBRARY");
 void *lib=dlopen(library&&*library?library:"/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);
 LOAD(int,CGLChoosePixelFormat,(const int *,void **,int *));LOAD(int,CGLCreateContext,(void *,void *,void **));
 LOAD(int,CGLSetCurrentContext,(void *));LOAD(int,CGLDestroyContext,(void *));LOAD(int,CGLDestroyPixelFormat,(void *));
 LOAD(int,CGLTexImageIOSurface2D,(void *,unsigned,unsigned,int,int,unsigned,unsigned,void *,unsigned));
 LOAD(void,glGenTextures,(int,unsigned *));LOAD(void,glBindTexture,(unsigned,unsigned));LOAD(void,glDeleteTextures,(int,const unsigned *));
 LOAD(void,glTexParameteri,(unsigned,unsigned,int));LOAD(void,glPixelStorei,(unsigned,int));
 LOAD(void,glGetTexImage,(unsigned,int,unsigned,unsigned,void *));LOAD(unsigned,glGetError,(void));
 LOAD(const char *,glGetString,(unsigned));LOAD(void,CGLSFFXTransferStats,(uint64_t *));
 int attributes[]={99,profile,73,96,0},count=0;void *pf=NULL,*ctx=NULL;
 REQUIRE(!CGLChoosePixelFormat(attributes,&pf,&count)&&!CGLCreateContext(pf,NULL,&ctx)&&!CGLSetCurrentContext(ctx));
 REQUIRE(glGetString(0x1f01)&&strstr(glGetString(0x1f01),"Apple M1"));
 unsigned texture[2]={0};glGenTextures(2,texture);glPixelStorei(0xd05,1);glPixelStorei(0xd02,0);
 uint64_t before[4],after[4];CGLSFFXTransferStats(before);unsigned compared=0;
 for(int revision=0;revision<2;revision++){
  char ready=0;REQUIRE(read(fd,&ready,1)==1&&ready=='0'+revision);
  IOSurfaceIncrementUseCount(s);REQUIRE(IOSurfaceGetUseCount(s)==1&&IOSurfaceIsInUse(s));
  for(int p=0;p<2;p++){
   glBindTexture(0x84f5,texture[p]);glTexParameteri(0x84f5,0x2801,0x2600);glTexParameteri(0x84f5,0x2800,0x2600);
   unsigned format=p?0x8227:0x1903;
   REQUIRE(!CGLTexImageIOSurface2D(ctx,0x84f5,format,width(p),height(p),format,0x1401,s,p));
  }
  // Release the MacIOSurface-style hold AFTER both CGL imports completed.
  IOSurfaceDecrementUseCount(s);REQUIRE(IOSurfaceGetUseCount(s)==0&&IOSurfaceIsInUse(s));
  REQUIRE(write(fd,&ready,1)==1);char overwritten=0;REQUIRE(read(fd,&overwritten,1)==1&&overwritten=='X');
  // Parent has now reused the source. Existing RECT textures must still show
  // this revision, including when the GPU image cache is reused next time.
  for(int p=0;p<2;p++){
   unsigned char pixels[W*H*2]={0};glBindTexture(0x84f5,texture[p]);glGetTexImage(0x84f5,0,p?0x8227:0x1903,0x1401,pixels);
   for(int y=0;y<height(p);y++)for(int x=0;x<width(p);x++)for(int c=0;c<(p?2:1);c++){
    REQUIRE(pixels[(y*width(p)+x)*(p?2:1)+c]==sample(p,x,y,c,revision));compared++;
   }
  }
  REQUIRE(glGetError()==0&&write(fd,&overwritten,1)==1);
 }
 CGLSFFXTransferStats(after);REQUIRE(after[0]==before[0]&&after[1]==before[1]&&after[2]>before[2]);
 glDeleteTextures(2,texture);REQUIRE(glGetError()==0&&IOSurfaceGetUseCount(s)==0&&!IOSurfaceIsInUse(s));
 printf("FXNV12 child profile=%x exact_bytes=%u cpu_upload=%llu cpu_readback=%llu gpu_blit=%llu reuse_after_completion=1 local_use=0 global_in_use=0 pass=1\n",profile,compared,(unsigned long long)(after[0]-before[0]),(unsigned long long)(after[1]-before[1]),(unsigned long long)(after[2]-before[2]));
 CFRelease(s);CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);dlclose(lib);return 0;
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);alarm(40);
 if(argc==5&&!strcmp(argv[1],"child"))return child(atoi(argv[2]),(uint32_t)strtoul(argv[3],NULL,10),(int)strtol(argv[4],NULL,0));
 size_t (*offset)(IOSurfaceRef,size_t)=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUGetPlaneOffset");REQUIRE(offset);
 for(int bad=1;bad<=4;bad++){IOSurfaceRef s=make(0x34323076,bad,bad==4);REQUIRE(!s);}
 const int profiles[]={0x3200,0x4100,0x1000};
 for(int layout=0;layout<2;layout++)for(int p=0;p<3;p++)for(int range=0;range<2;range++){
  IOSurfaceRef s=make(range?0x34323066:0x34323076,0,layout);REQUIRE(s&&offset(s,0)==16384+(layout?512:0)&&offset(s,1)==16384+UVOFF&&offset(s,2)==0);
  int channel[2];REQUIRE(!socketpair(AF_UNIX,SOCK_STREAM,0,channel));
  char fd[20],id[20],profile[20];snprintf(fd,sizeof(fd),"%d",channel[1]);snprintf(id,sizeof(id),"%u",IOSurfaceGetID(s));snprintf(profile,sizeof(profile),"%d",profiles[p]);
  fill(s,0);IOSurfaceIncrementUseCount(s);
  char *args[]={argv[0],"child",fd,id,profile,NULL};pid_t pid=0;posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_addclose(&actions,channel[0]);
  int error=posix_spawn(&pid,argv[0],&actions,NULL,args,environ);posix_spawn_file_actions_destroy(&actions);close(channel[1]);REQUIRE(!error);
  for(int revision=0;revision<2;revision++){
   char value='0'+revision,reply=0;REQUIRE(write(channel[0],&value,1)==1&&read(channel[0],&reply,1)==1&&reply==value);
   REQUIRE(IOSurfaceGetUseCount(s)==1&&IOSurfaceIsInUse(s));IOSurfaceDecrementUseCount(s);
   REQUIRE(IOSurfaceGetUseCount(s)==0&&!IOSurfaceIsInUse(s));
   fill(s,revision+1);value='X';REQUIRE(write(channel[0],&value,1)==1&&read(channel[0],&reply,1)==1&&reply==value);
   if(revision==0)IOSurfaceIncrementUseCount(s);
  }
  close(channel[0]);int status=0;REQUIRE(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
  printf("FXNV12 parent profile=%s range=%s odd_visible=%dx%d stride=%d y_offset=%d top_stride=%d uv_offset=%d padded_allocation=%d cross_exec=1 local_use=0 global_in_use=0 pass=1\n",profile,range?"full":"video",W,H,STRIDE,layout?512:0,layout?192:STRIDE,UVOFF,ALLOC);
  CFRelease(s);
 }
 printf("FXNV12 malformed_layouts_rejected=4 cases=12 pass=1\n");return 0;
}
