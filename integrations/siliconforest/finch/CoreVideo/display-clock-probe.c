/* SPDX-License-Identifier: MIT */
/* Own public C ABI declarations. Read actual callback arrival and timestamp
 * clocks independently; no window, GPU commands, input or provider mutation. */
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef void *Link;
typedef int32_t Return;
typedef struct { int64_t value; int32_t scale,flags; } CVTime;
typedef struct {int16_t subframes,divisor;uint32_t counter,type,flags;int16_t hours,minutes,seconds,frames;} SMPTE;
typedef struct {uint32_t version;int32_t scale;int64_t video;uint64_t host;double rate;int64_t period;SMPTE smpte;uint64_t flags,reserved;} Stamp;
_Static_assert(sizeof(Stamp)==80,"public CVTimeStamp declaration");
typedef Return (*Callback)(Link,const Stamp*,const Stamp*,uint64_t,uint64_t*,void*);
enum {samples=121};
static struct {
 pthread_mutex_t mutex;
 pthread_cond_t ready;
 unsigned count;
 uint64_t arrival[samples],inHost[samples],outHost[samples];
 int64_t video[samples],period[samples];
 int32_t scale[samples];
} state={.mutex=PTHREAD_MUTEX_INITIALIZER,.ready=PTHREAD_COND_INITIALIZER};
static Return receive(Link link,const Stamp *now,const Stamp *out,uint64_t flags,uint64_t *output,void *context) {
 (void)link;(void)flags;(void)output;(void)context;
 uint64_t arrived=mach_absolute_time();
 pthread_mutex_lock(&state.mutex);
 if(state.count<samples) {
  unsigned i=state.count++;
  state.arrival[i]=arrived;state.inHost[i]=now->host;state.outHost[i]=out->host;
  state.video[i]=now->video;state.period[i]=now->period;state.scale[i]=now->scale;
  if(state.count==samples)pthread_cond_signal(&state.ready);
 }
 pthread_mutex_unlock(&state.mutex);return 0;
}
static int resolve(void *image,void *slot,const char *name) {
 *(void **)slot=dlsym(image,name);
 if(!*(void **)slot)fprintf(stderr,"DISPLAY_CLOCK missing=%s\n",name);
 return *(void **)slot!=NULL;
}
#define LOAD(var,name) do{if(!resolve(image,&var,name))return 77;}while(0)
int main(void) {
 void *image=dlopen("/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo",RTLD_NOW|RTLD_GLOBAL);
 if(!image){fprintf(stderr,"DISPLAY_CLOCK load=%s\n",dlerror());return 77;}
 Return (*create)(Link*),(*setCallback)(Link,Callback,void*),(*start)(Link),(*stop)(Link);
 void (*release)(Link);CVTime (*nominal)(Link);double (*actual)(Link);
 LOAD(create,"CVDisplayLinkCreateWithActiveCGDisplays");LOAD(setCallback,"CVDisplayLinkSetOutputCallback");
 LOAD(start,"CVDisplayLinkStart");LOAD(stop,"CVDisplayLinkStop");LOAD(release,"CVDisplayLinkRelease");
 LOAD(nominal,"CVDisplayLinkGetNominalOutputVideoRefreshPeriod");LOAD(actual,"CVDisplayLinkGetActualOutputVideoRefreshPeriod");
 Link link=NULL;Return result=create(&link);printf("CREATE status=%d link=%d\n",result,link!=NULL);
 if(result||!link)return 1;
 CVTime period=nominal(link);double advertisedBefore=actual(link);
 result=setCallback(link,receive,NULL);if(result){release(link);return 1;}
 result=start(link);printf("START status=%d\n",result);if(result){release(link);return 1;}
 struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=5;
 pthread_mutex_lock(&state.mutex);
 while(state.count<samples)if(pthread_cond_timedwait(&state.ready,&state.mutex,&deadline))break;
 pthread_mutex_unlock(&state.mutex);
 double advertisedAfter=actual(link);result=stop(link);
 mach_timebase_info_data_t tb;mach_timebase_info(&tb);
 pthread_mutex_lock(&state.mutex);
 printf("CLOCK count=%u timebase=%u/%u nominal=%lld/%d flags=%d advertised-before=%.9f advertised-after=%.9f stop=%d\n",state.count,tb.numer,tb.denom,(long long)period.value,period.scale,period.flags,advertisedBefore,advertisedAfter,result);
 int bad=state.count!=samples||result!=0||!tb.denom;
 for(unsigned i=1;i<state.count;i++) {
  if(state.arrival[i]<state.arrival[i-1]||state.inHost[i]<state.inHost[i-1])bad=1;
  printf("TICK %u arrival-ns=%.0Lf input-delta-ns=%.0Lf output-ahead-ns=%.0Lf video-delta=%lld scale=%d refresh=%lld\n",i,
   (long double)(state.arrival[i]-state.arrival[i-1])*tb.numer/tb.denom,
   (long double)(state.inHost[i]-state.inHost[i-1])*tb.numer/tb.denom,
   ((long double)state.outHost[i]-state.inHost[i])*tb.numer/tb.denom,
   (long long)(state.video[i]-state.video[i-1]),state.scale[i],(long long)state.period[i]);
 }
 pthread_mutex_unlock(&state.mutex);release(link);
 /* Keep the image and static callback state alive until process exit: retained
  * PD Stop is asynchronous. Never dlclose code beneath a callback thread. */
 return bad;
}
