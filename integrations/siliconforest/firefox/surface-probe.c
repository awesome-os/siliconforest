/* SPDX-License-Identifier: MIT
 * Exec-separated global ID pixel sharing; local surfaces must remain private.
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
typedef struct __IOSurface *IOSurfaceRef;
extern IOSurfaceRef IOSurfaceCreate(CFDictionaryRef),IOSurfaceLookup(uint32_t);
extern uint32_t IOSurfaceGetID(IOSurfaceRef);
extern void *IOSurfaceGetBaseAddress(IOSurfaceRef);
extern size_t IOSurfaceGetAllocSize(IOSurfaceRef);
extern char **environ;
static uint32_t hash(IOSurfaceRef s){
 unsigned char *p=IOSurfaceGetBaseAddress(s);size_t n=IOSurfaceGetAllocSize(s);uint32_t h=2166136261u;
 for(size_t i=0;i<n;i++)h=(h^p[i])*16777619u;
 return h;
}
int main(int argc,char **argv){
 if(argc==5&&!strcmp(argv[1],"child")){
  IOSurfaceRef s=IOSurfaceLookup((uint32_t)strtoul(argv[2],NULL,10));
  int expected=atoi(argv[3]);uint32_t want=(uint32_t)strtoul(argv[4],NULL,10);
  size_t mismatches=0;
  if(s){
   size_t size=IOSurfaceGetAllocSize(s);const unsigned char *bytes=IOSurfaceGetBaseAddress(s);
   if(size!=2048)mismatches++;
   for(size_t i=0;i<size;i++)mismatches+=bytes[i]!=(unsigned char)(i*37+(i>>5));
  }
  int good=expected?(s&&hash(s)==want&&!mismatches):!s;
  printf("FXSURFACE child expected_global=%d mapped=%d byte_mismatches=%zu pixels_equal=%d\n",expected,s!=NULL,mismatches,good);
  if(s)CFRelease(s);return good?0:1;
 }
 int requireGPU=argc==2&&!strcmp(argv[1],"--require-gpu");
 int (*copyGPUFD)(IOSurfaceRef)=dlsym(RTLD_DEFAULT,"IOSurfaceSFGPUCopyFD");
 int failures=0;
 for(int global=0;global<=1;global++){
  CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  const char *keys[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement","IOSurfacePixelFormat"};int values[]={32,16,4,0x42475241};
  for(int i=0;i<4;i++){
   CFStringRef key=CFStringCreateWithCString(NULL,keys[i],kCFStringEncodingUTF8);
   CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);CFDictionarySetValue(props,key,n);CFRelease(n);CFRelease(key);
  }
  CFDictionarySetValue(props,CFSTR("IOSurfaceIsGlobal"),global?kCFBooleanTrue:kCFBooleanFalse);
  IOSurfaceRef s=IOSurfaceCreate(props);CFRelease(props);if(!s)return 2;
  int gpuFD=copyGPUFD?copyGPUFD(s):-1;
  printf("FXSURFACE parent global=%d gpu_backing=%d\n",global,gpuFD>=0);
  if(gpuFD>=0)close(gpuFD);else if(requireGPU){CFRelease(s);return 4;}
  size_t size=IOSurfaceGetAllocSize(s);unsigned char *bytes=IOSurfaceGetBaseAddress(s);
  for(size_t i=0;i<size;i++)bytes[i]=(unsigned char)(i*37+(i>>5));
  char id[32],mode[8],want[32];snprintf(id,sizeof(id),"%u",IOSurfaceGetID(s));snprintf(mode,sizeof(mode),"%d",global);snprintf(want,sizeof(want),"%u",hash(s));
  char *child_argv[]={argv[0],"child",id,mode,want,NULL};pid_t child=0;int status=0;
  int error=posix_spawn(&child,argv[0],NULL,NULL,child_argv,environ);
  if(error){CFRelease(s);return 3;}
  int reaped=0;
  for(int i=0;i<120;i++){
   if(waitpid(child,&status,WNOHANG)==child){reaped=1;break;}usleep(50000);
  }
  if(!reaped){kill(child,SIGKILL);waitpid(child,&status,0);failures++;}
  else if(!WIFEXITED(status)||WEXITSTATUS(status))failures++;
  printf("FXSURFACE parent global=%d bytes=%zu high_id=%d child_status=%d\n",global,size,(IOSurfaceGetID(s)&0x80000000u)!=0,status);
  CFRelease(s);
 }
 printf("FXSURFACE failures=%d\n",failures);return failures?1:0;
}
