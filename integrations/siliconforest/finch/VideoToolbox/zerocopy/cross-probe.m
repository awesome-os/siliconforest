/* SPDX-License-Identifier: MIT */
/* Cross-process public IOSurface usecount bridge, not a GPU-fence or pixel
 * aliasing test. Lookup/increment must complete BEFORE the producer releases
 * its lease; no late-ID consumer is assumed to be safe. */
#import <Foundation/Foundation.h>
#include "lease.h"
#include <libavutil/buffer.h>
#include <spawn.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
extern char **environ;
extern int _NSGetExecutablePath(char *,uint32_t *);
/* Public ABI declarations missing from our minimal IOSurface header. */
extern IOSurfaceRef IOSurfaceLookup(uint32_t);
extern const CFStringRef kIOSurfaceIsGlobal;
extern void IOSurfaceIncrementUseCount(IOSurfaceRef);
extern void IOSurfaceDecrementUseCount(IOSurfaceRef);
extern int32_t IOSurfaceGetUseCount(IOSurfaceRef);
extern bool IOSurfaceIsInUse(IOSurfaceRef);
static unsigned freed;
static unsigned freeCount(void){return __atomic_load_n(&freed,__ATOMIC_ACQUIRE);}
static void freeBytes(void *unused,uint8_t *bytes){(void)unused;free(bytes);__atomic_fetch_add(&freed,1,__ATOMIC_RELEASE);}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int sendByte(int fd,char value){ssize_t n;do{n=write(fd,&value,1);}while(n<0&&errno==EINTR);return n==1;}
static int receiveByte(int fd,char *value,double timeout){
 double until=now()+timeout;
 for(;;){
  double left=until-now();if(left<=0)return 0;
  struct pollfd p={fd,POLLIN,0};int result=poll(&p,1,(int)(left*1000)+1);
  if(result<0&&errno==EINTR)continue;
  if(result<=0)return 0;
  if(p.revents&(POLLIN|POLLHUP)){ssize_t n;do{n=read(fd,value,1);}while(n<0&&errno==EINTR);return n==1;}
  return 0;
 }
}
static int reap(pid_t child,int *status,double timeout){
 double until=now()+timeout;
 while(now()<until){pid_t result=waitpid(child,status,WNOHANG);if(result==child)return 1;if(result<0&&errno!=EINTR)return 0;usleep(5000);}return 0;
}
static void closeFD(int *fd){if(*fd>=0){close(*fd);*fd=-1;}}
/* This opt-in checks the FX provider's concrete backing hooks in BOTH
 * processes. It does not establish pixel aliasing or GPU completion. */
static int checkRequiredGPUBacking(IOSurfaceRef surface,const char *role){
 const char *required=getenv("SF_CROSS_REQUIRE_GPU");
 if(!required||strcmp(required,"1"))return 0;
 int (*copyFD)(IOSurfaceRef)=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUCopyFD");
 size_t (*getOffset)(IOSurfaceRef)=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUGetOffset");
 if(!copyFD||!getOffset){
  fprintf(stderr,"CROSS_GPU_UNSUPPORTED role=%s copy-fd-symbol=%d offset-symbol=%d\n",role,copyFD!=NULL,getOffset!=NULL);return 77;
 }
 int fd=copyFD(surface);size_t offset=getOffset(surface);
 int valid=fd>=0&&fcntl(fd,F_GETFD)>=0;
 int closed=fd>=0?close(fd)==0:0;
 printf("CROSS_GPU_BACKING role=%s gpu-fd-valid=%d data-offset=%zu temporary-fd-closed=%d required-offset=16384\n",role,valid,offset,closed);
 if(!valid||!closed||offset!=16384){fprintf(stderr,"CROSS_GPU_UNSUPPORTED role=%s actual GPU backing requirement failed\n",role);return 77;}
 return 0;
}
/* Normalize away from the fixed consumer descriptors3/4. dup2 actions then
 * cannot accidentally close their own final descriptors through pipe aliases. */
static int makePipe(int ends[2]){
 int raw[2];if(pipe(raw))return 0;ends[0]=ends[1]=-1;
 for(unsigned i=0;i<2;i++){
#ifdef F_DUPFD_CLOEXEC
  ends[i]=fcntl(raw[i],F_DUPFD_CLOEXEC,10);
#else
  ends[i]=fcntl(raw[i],F_DUPFD,10);
  if(ends[i]>=0&&fcntl(ends[i],F_SETFD,FD_CLOEXEC)){closeFD(&ends[i]);}
#endif
  if(ends[i]<0){close(raw[0]);close(raw[1]);closeFD(&ends[0]);closeFD(&ends[1]);return 0;}
 }
 close(raw[0]);close(raw[1]);return 1;
}
static void number(CFMutableDictionaryRef d,CFStringRef key,int64_t value){CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&value);CFDictionarySetValue(d,key,n);CFRelease(n);}
static int consumer(const char *argument,int exitWithoutRelease){
 IOSurfaceRef surface=NULL;int incremented=0,result=1;char *end=NULL;
 errno=0;unsigned long value=strtoul(argument,&end,10);
 if(errno||!argument[0]||!end||*end||!value||value>UINT32_MAX){fprintf(stderr,"CROSS_LEASE_FAIL invalid consumer surface ID\n");goto done;}
 surface=IOSurfaceLookup((uint32_t)value);
 if(!surface){fprintf(stderr,"CROSS_LEASE_UNSUPPORTED global IOSurfaceLookup refused\n");sendByte(3,'U');result=77;goto done;}
 if(IOSurfaceGetID(surface)!=(uint32_t)value){fprintf(stderr,"CROSS_LEASE_FAIL lookup identity\n");sendByte(3,'E');goto done;}
 if(checkRequiredGPUBacking(surface,"consumer")){sendByte(3,'G');result=77;goto done;}
 IOSurfaceIncrementUseCount(surface);incremented=1;
 int32_t count=IOSurfaceGetUseCount(surface);
 printf("CROSS_CHILD held-usecount=%d\n",count);
 if(count<1){fprintf(stderr,"CROSS_LEASE_FAIL child count after increment=%d expected-positive\n",count);sendByte(3,'E');goto done;}
 if(!sendByte(3,'H')){fprintf(stderr,"CROSS_LEASE_FAIL child handshake H\n");goto done;}
 char request=0;
 if(!receiveByte(4,&request,5)||request!=(exitWithoutRelease?'X':'R')){fprintf(stderr,"CROSS_LEASE_FAIL child release handshake\n");goto done;}
 if(exitWithoutRelease){
  /* Deliberately bypass CF/Objective-C cleanup and the explicit decrement.
   * Only process-death bookkeeping may clear this consumer's usecount. */
  printf("CROSS_CHILD process-exit-with-usecount=%d no-explicit-decrement=1 no-CFRelease=1\n",IOSurfaceGetUseCount(surface));
  _exit(0);
 }
 IOSurfaceDecrementUseCount(surface);incremented=0;count=IOSurfaceGetUseCount(surface);
 CFRelease(surface);surface=NULL;
 printf("CROSS_CHILD released-usecount=%d\n",count);
 if(count!=0){fprintf(stderr,"CROSS_LEASE_FAIL child final count\n");goto done;}
 result=0;
 done:
 if(surface){if(incremented)IOSurfaceDecrementUseCount(surface);CFRelease(surface);}
 close(3);close(4);return result;
}
#define REQUIRE(condition,what) do{if(!(condition)){fprintf(stderr,"CROSS_LEASE_FAIL %s errno=%d\n",what,errno);result=1;goto cleanup;}}while(0)
static int producer(int predicateOnly,int consumerExit){
 IOSurfaceRef surface=NULL;CFMutableDictionaryRef properties=NULL;CFTypeRef lease=NULL;AVFrame *frame=NULL;uint8_t *bytes=NULL;
 int held[2]={-1,-1},release[2]={-1,-1};pid_t child=-1;int childStatus=0,result=1;
 posix_spawn_file_actions_t actions;int actionsReady=0;
 properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);REQUIRE(properties,"surface dictionary");
 number(properties,kIOSurfaceWidth,4);number(properties,kIOSurfaceHeight,4);number(properties,kIOSurfaceBytesPerRow,16);number(properties,kIOSurfaceBytesPerElement,4);number(properties,kIOSurfacePixelFormat,0x42475241);
 CFDictionarySetValue(properties,kIOSurfaceIsGlobal,kCFBooleanTrue);surface=IOSurfaceCreate(properties);CFRelease(properties);properties=NULL;
 REQUIRE(surface,"source-owned global surface");REQUIRE(IOSurfaceGetID(surface)!=0,"surface ID");
 if(checkRequiredGPUBacking(surface,"producer")){result=77;goto cleanup;}
 REQUIRE(IOSurfaceGetUseCount(surface)==0&&!IOSurfaceIsInUse(surface),"initial surface usecount0 and not in use");
 frame=av_frame_alloc();REQUIRE(frame,"frame");bytes=calloc(1,64);REQUIRE(bytes,"tracked frame payload");
 frame->buf[0]=av_buffer_create(bytes,64,freeBytes,NULL,0);REQUIRE(frame->buf[0],"frame AVBuffer");frame->data[0]=bytes;bytes=NULL;
 lease=SFVTCreateFrameLease(frame,surface);REQUIRE(lease,"producer lease");av_frame_free(&frame);
 REQUIRE(IOSurfaceGetUseCount(surface)==1&&freeCount()==0,"producer count1 holds the only AVBuffer reference");
 printf("CROSS_PARENT producer-usecount=1 AVBuffer-free=0\n");
 REQUIRE(makePipe(held)&&makePipe(release),"handshake pipes");
 REQUIRE(!posix_spawn_file_actions_init(&actions),"spawn file actions");actionsReady=1;
 REQUIRE(!posix_spawn_file_actions_adddup2(&actions,held[1],3)&&!posix_spawn_file_actions_adddup2(&actions,release[0],4),"spawn fixed handshake descriptors");
 for(unsigned i=0;i<2;i++)REQUIRE(!posix_spawn_file_actions_addclose(&actions,held[i])&&!posix_spawn_file_actions_addclose(&actions,release[i]),"spawn close original pipe descriptors");
 char executable[PATH_MAX],resolved[PATH_MAX],identifier[32];uint32_t length=sizeof(executable);
 REQUIRE(!_NSGetExecutablePath(executable,&length)&&realpath(executable,resolved),"same executable path");
 snprintf(identifier,sizeof(identifier),"%u",IOSurfaceGetID(surface));char *argv[]={resolved,consumerExit?"--consumer-exit":"--consumer",identifier,NULL};
 int spawned=posix_spawn(&child,resolved,&actions,NULL,argv,environ);
 if(spawned){errno=spawned;child=-1;}REQUIRE(!spawned&&child>0,"same-binary posix_spawn");
 closeFD(&held[1]);closeFD(&release[0]);
 char response=0;REQUIRE(receiveByte(held[0],&response,5),"consumer handshake timeout");
 if(response=='U'){result=77;fprintf(stderr,"CROSS_LEASE_UNSUPPORTED consumer cannot look up live producer surface\n");goto cleanup;}
 if(response=='G'){result=77;fprintf(stderr,"CROSS_GPU_UNSUPPORTED consumer actual GPU backing requirement failed\n");goto cleanup;}
 REQUIRE(response=='H',"consumer successfully looked up and counted the live surface");
 int32_t beforeRelease=IOSurfaceGetUseCount(surface);
 printf("CROSS_PARENT after-H observed-usecount=%d is-in-use=%d AVBuffer-free=%u\n",beforeRelease,IOSurfaceIsInUse(surface),freeCount());
 REQUIRE(!freeCount(),"producer lease still holds AVBuffer before release");
 CFRelease(lease);lease=NULL;
 usleep(30000);
 int32_t afterRelease=IOSurfaceGetUseCount(surface);
 int heldInUse=IOSurfaceIsInUse(surface);
 printf("CROSS_PARENT producer-released=1 observed-usecount=%d is-in-use=%d AVBuffer-free=%u\n",afterRelease,heldInUse,freeCount());
 REQUIRE(heldInUse,"global predicate sees explicit consumer H hold after producer release");
 if(!predicateOnly)REQUIRE(!freeCount(),"consumer keeps AVBuffer after producer release and worker polling");
 REQUIRE(sendByte(release[1],consumerExit?'X':'R'),"release consumer only after held-buffer assertion");closeFD(&release[1]);
 REQUIRE(reap(child,&childStatus,5),"consumer exit deadline");child=-1;
 REQUIRE(WIFEXITED(childStatus)&&WEXITSTATUS(childStatus)==0,"consumer normal exit0");
 /* Process teardown can finish after waitpid observes the exit. Unlike the
  * explicit R/decrement control, give only this death-control a bounded wait. */
 if(consumerExit)for(unsigned n=0;n<400&&IOSurfaceIsInUse(surface);n++)usleep(5000);
 int finalInUse=IOSurfaceIsInUse(surface);
 printf("CROSS_PARENT after-%c observed-usecount=%d is-in-use=%d AVBuffer-free=%u\n",consumerExit?'X':'R',IOSurfaceGetUseCount(surface),finalInUse,freeCount());
 REQUIRE(!finalInUse,consumerExit?"global predicate clears after owned consumer process death":"global predicate clears after explicit consumer R completion");
 uint64_t issued=0,released=0,deferred=0;
 for(unsigned n=0;n<400;n++){SFVTFrameLeaseCounts(&issued,&released,&deferred);if(released==1)break;usleep(5000);}
 REQUIRE(issued==1&&released==1&&freeCount()==1&&IOSurfaceGetUseCount(surface)==0,"AVBuffer released exactly once");
 if(predicateOnly)puts("CROSS_PREDICATE_PASS child-before-R=in-use child-after-R=not-in-use no-frame-lifetime-claim=1 no-GPU-fence-claim=1");
 else {REQUIRE(deferred==1,"consumer required deferred AVBuffer release");
  if(consumerExit)puts("CROSS_EXIT_PASS consumer-exited-with-count-held=1 global-ownership-cleared=1 AVBuffer-released-once=1 no-GPU-fence-claim=1");
  else puts("CROSS_LEASE_PASS cross-process-global-predicate=1 producer-before-H=held consumer-before-R=held AVBuffer-released-once=1 no-GPU-fence-claim=1");
 }result=0;
 cleanup:
 if(actionsReady)posix_spawn_file_actions_destroy(&actions);
 /* Only our spawned child is addressed. Request its explicit R release or X
  * process exit, then kill/reap after a finite deadline. Never forge a count. */
 if(child>0){if(release[1]>=0)sendByte(release[1],consumerExit?'X':'R');closeFD(&release[1]);if(!reap(child,&childStatus,1)){kill(child,SIGKILL);if(!reap(child,&childStatus,2)){fprintf(stderr,"CROSS_LEASE_FAIL owned child not reaped after SIGKILL\n");result=1;}}}
 closeFD(&held[0]);closeFD(&held[1]);closeFD(&release[0]);closeFD(&release[1]);
 if(lease)CFRelease(lease);if(frame)av_frame_free(&frame);free(bytes);if(properties)CFRelease(properties);if(surface)CFRelease(surface);return result;
}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IOLBF,0);signal(SIGPIPE,SIG_IGN);@autoreleasepool{
 if(argc==3&&!strcmp(argv[1],"--consumer"))return consumer(argv[2],0);
 if(argc==3&&!strcmp(argv[1],"--consumer-exit"))return consumer(argv[2],1);
 if(argc==2&&!strcmp(argv[1],"--predicate"))return producer(1,0);
 if(argc==2&&!strcmp(argv[1],"--process-exit"))return producer(0,1);
 if(argc!=1){fprintf(stderr,"usage: cross-probe [--predicate | --process-exit | --consumer surfaceID | --consumer-exit surfaceID]\n");return 2;}
 return producer(0,0);
}}
