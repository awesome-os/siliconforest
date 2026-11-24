/* SPDX-License-Identifier: MIT */
#include "sc-route-source.h"
#include <CoreFoundation/CFRuntime.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
static unsigned calls,masks,freed;
static void dead(CFTypeRef value){(void)value;freed++;}
static void notified(CFTypeRef target,unsigned changes){if(target){calls++;masks|=changes;}}
static void pump(double seconds){CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);}
static int waitMask(unsigned mask){for(int i=0;i<50&&!(masks&mask);i++)pump(.01);return !!(masks&mask);}
static double cpu(void){struct rusage r;getrusage(RUSAGE_SELF,&r);return r.ru_utime.tv_sec+r.ru_stime.tv_sec+(r.ru_utime.tv_usec+r.ru_stime.tv_usec)*1e-6;}
static int descriptors(void){int n=0;for(int i=0;i<512;i++)if(fcntl(i,F_GETFD)>=0)n++;return n;}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 const char *path=argv[1];FILE *f=fopen(path,"w");if(!f)return 3;fputs("nameserver 127.0.0.1\n",f);fclose(f);
 static const CFRuntimeClass cls={.version=0,.className="SFSCProbeTarget",.finalize=dead};
 CFTypeID type=_CFRuntimeRegisterClass(&cls);int failures=0,warm=-1;
 for(int i=0;i<65;i++){
  unsigned beforeFreed=freed;CFTypeRef target=_CFRuntimeCreateInstance(NULL,type,0,NULL);
  CFRunLoopSourceRef source=SFSCNetworkSourceCreate(NULL,target,0,notified,path);CFRelease(target);
  if(!source){fprintf(stderr,"source create failed iteration=%d\n",i);return 4;}
  calls=masks=0;CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode);
  if(!waitMask(SF_SC_ROUTE_CHANGED|SF_SC_DNS_CHANGED))failures++;
  if(i==0){
   masks=0;f=fopen(path,"w");fputs("nameserver 127.0.0.2\n",f);fclose(f);
   if(!waitMask(SF_SC_DNS_CHANGED))failures++;
   masks=0;char next[4096];snprintf(next,sizeof(next),"%s.next",path);
   f=fopen(next,"w");fputs("nameserver 127.0.0.3\n",f);fclose(f);if(rename(next,path))return 5;
   if(!waitMask(SF_SC_DNS_CHANGED))failures++;
   masks=0;f=fopen(path,"w");fputs("nameserver 127.0.0.4\n",f);fclose(f);
   if(!waitMask(SF_SC_DNS_CHANGED))failures++;
   pump(.05);masks=0;unsigned count=calls;double start=cpu();pump(.5);double elapsed=cpu()-start;
   printf("SFSC idle_cpu_seconds=%.6f notifications_before=%u after=%u\n",elapsed,count,calls);
   if((masks&SF_SC_DNS_CHANGED)||elapsed>.05)failures++;
  }
  CFRunLoopRemoveSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode);CFRelease(source);
  pump(.002);if(freed!=beforeFreed+1)failures++;
  if(i==0){warm=descriptors();printf("SFSC warm_descriptors=%d\n",warm);}
 }
 if(descriptors()!=warm)failures++;
 printf("SFSC cycles=65 target_finalizers=%u descriptors=%d failures=%d\n",freed,descriptors(),failures);
 unlink(path);return failures?1:0;
}
