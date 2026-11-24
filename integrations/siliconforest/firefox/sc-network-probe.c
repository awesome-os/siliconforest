/* SPDX-License-Identifier: MIT */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/resource.h>
static int retained,released,reachCalls,storeCalls,failures,selfCancel,selfCancelled;
static CFRunLoopSourceRef selfStoreSource;
static const void *keep(const void *p){retained++;return p;}
static void drop(const void *p){(void)p;released++;}
static void reach(SCNetworkReachabilityRef target,SCNetworkReachabilityFlags flags,void *info){
 if(!target||info!=(void *)123||!(flags&kSCNetworkReachabilityFlagsReachable))failures++;reachCalls++;
 if(selfCancel){if(!SCNetworkReachabilityUnscheduleFromRunLoop(target,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode))failures++;selfCancelled++;}
}
static void store(SCDynamicStoreRef target,CFArrayRef keys,void *info){
 if(!target||CFGetTypeID(target)!=SCDynamicStoreGetTypeID()||info!=(void *)123||!CFArrayGetCount(keys))failures++;storeCalls++;
 if(selfStoreSource){CFRunLoopRemoveSource(CFRunLoopGetCurrent(),selfStoreSource,kCFRunLoopDefaultMode);CFRelease(selfStoreSource);selfStoreSource=NULL;}
}
static int fds(void){int n=0;for(int i=0;i<512;i++)if(fcntl(i,F_GETFD)>=0)n++;return n;}
static void pump(double seconds){CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);}
static double cpu(void){struct rusage r;getrusage(RUSAGE_SELF,&r);return r.ru_utime.tv_sec+r.ru_stime.tv_sec+(r.ru_utime.tv_usec+r.ru_stime.tv_usec)*1e-6;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failures++;}}while(0)
int main(void){
 int warm=-1;
 for(int i=0;i<65;i++){
  selfCancel=(i==64);
  struct sockaddr_in address={.sin_len=sizeof(address),.sin_family=AF_INET};
  SCNetworkReachabilityRef r=SCNetworkReachabilityCreateWithAddress(NULL,(void *)&address);CHECK(r);if(!r)return 2;
  SCNetworkReachabilityContext rc={0,(void *)123,keep,drop,NULL};
  CHECK(SCNetworkReachabilitySetCallback(r,reach,&rc));
  SCNetworkReachabilityContext invalid=rc;invalid.version=1;CHECK(!SCNetworkReachabilitySetCallback(r,reach,&invalid));
  CFRunLoopRef loop=CFRunLoopGetCurrent();
  CHECK(SCNetworkReachabilityScheduleWithRunLoop(r,loop,kCFRunLoopDefaultMode));
  CHECK(SCNetworkReachabilityScheduleWithRunLoop(r,loop,kCFRunLoopDefaultMode));
  SCDynamicStoreContext dc={0,(void *)123,keep,drop,NULL};
  SCDynamicStoreRef s=SCDynamicStoreCreate(NULL,CFSTR("SiliconForest native Firefox network probe"),store,&dc);CHECK(s);if(!s)return 3;
  CFStringRef pattern=CFSTR("State:/Network/Service/[^/]+/IPv4");
  CFArrayRef patterns=CFArrayCreate(NULL,(const void **)&pattern,1,&kCFTypeArrayCallBacks);
  CHECK(SCDynamicStoreSetNotificationKeys(s,NULL,patterns));CFRelease(patterns);
  CFRunLoopSourceRef source=SCDynamicStoreCreateRunLoopSource(NULL,s,0);CHECK(source);if(!source)return 4;
  if(selfCancel)selfStoreSource=source;
  CFRunLoopAddSource(loop,source,kCFRunLoopDefaultMode);
  int beforeR=reachCalls,beforeS=storeCalls;
  for(int j=0;j<100&&(reachCalls==beforeR||storeCalls==beforeS);j++)pump(.01);
  CHECK(reachCalls>beforeR);CHECK(storeCalls>beforeS);
  if(!i){double start=cpu();int before=reachCalls;pump(.5);double used=cpu()-start;
   printf("SFSC backend_idle_cpu=%.6f reach_calls_before=%d after=%d\n",used,before,reachCalls);CHECK(used<.05);
   /* No invented general configd store: reads and nonnetwork keys fail. */
   CFPropertyListRef value=SCDynamicStoreCopyValue(s,CFSTR("State:/Network/Global/IPv4"));CHECK(!value);CHECK(SCError()==kSCStatusNoStoreServer);
   pattern=CFSTR("State:/Unimplemented/Thing");patterns=CFArrayCreate(NULL,(const void **)&pattern,1,&kCFTypeArrayCallBacks);
   CHECK(!SCDynamicStoreSetNotificationKeys(s,NULL,patterns));CFRelease(patterns);
  }
  CHECK(SCNetworkReachabilityUnscheduleFromRunLoop(r,loop,kCFRunLoopDefaultMode)==!selfCancel);
  CHECK(!SCNetworkReachabilityUnscheduleFromRunLoop(r,loop,kCFRunLoopDefaultMode));
  if(!selfCancel){CFRunLoopRemoveSource(loop,source,kCFRunLoopDefaultMode);CFRelease(source);}
  else CHECK(selfCancelled==1&&!selfStoreSource);
  CFRelease(s);CFRelease(r);
  pump(.002);CHECK(retained==released);
  if(!i)warm=fds();CHECK(fds()==warm);
 }
 printf("SFSC backend_cycles=65 reach_callbacks=%d store_callbacks=%d retains=%d releases=%d descriptors=%d failures=%d\n",reachCalls,storeCalls,retained,released,fds(),failures);
 return failures?1:0;
}
