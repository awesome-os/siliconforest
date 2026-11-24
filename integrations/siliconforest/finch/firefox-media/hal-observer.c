/* SPDX-License-Identifier: MIT */
/* Opt-in API-boundary observation only. No output creation, signals, device
 * changes or calls through an AudioUnit after disposal. Keep this image loaded
 * for the process lifetime. HAL clock scope is process/device, not per unit. */
#ifdef SF_MEDIA_OBSERVER_HOST_TEST
#include "observer-host-stubs.h"
#else
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include "SFAudioClient.h"
#endif
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>

_Static_assert(sizeof(struct sf_audio_ring)==48,"PD ring-v2 header");
_Static_assert(offsetof(struct sf_audio_ring,readFrame)==32,"PD ring-v2 cursor");

enum { MAX_UNITS=16,MAX_CALLBACKS=128 };
typedef OSStatus (*unit_operation)(AudioUnit);
static unit_operation next_start,next_stop,next_uninitialize,next_dispose;
static OSStatus (*next_property)(AudioUnit,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,void *,UInt32 *);
static OSStatus (*next_set_property)(AudioUnit,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,const void *,UInt32);
static OSStatus (*device_clock)(AudioObjectID,AudioTimeStamp *);
static const struct sf_audio_session *(*connected_session)(AudioObjectID);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned enabled,worker_running;
static double duration=125;
static uint64_t sequence,generation;
struct unit_state { AudioUnit token;AudioDeviceID device;unsigned active,disposed;uint64_t generation;double changed; };
static struct unit_state units[MAX_UNITS];
/* Stable process-lifetime contexts: never free a callback refCon while a
 * renderer could still be using it. Bounded diagnostic capacity, no PCM copy. */
struct callback_state {AudioUnit unit;AURenderCallbackStruct original;unsigned installed;uint64_t calls,frames,successes,errors,last_host;};
static struct callback_state callbacks[MAX_CALLBACKS];
static unsigned callback_count;

static double clock_seconds(clockid_t which){struct timespec t;clock_gettime(which,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void resolve(void){
#ifdef SF_MEDIA_OBSERVER_HOST_TEST
 next_start=dlsym(RTLD_NEXT,"AudioOutputUnitStart");
 next_stop=dlsym(RTLD_NEXT,"AudioOutputUnitStop");
 next_uninitialize=dlsym(RTLD_NEXT,"AudioUnitUninitialize");
 next_dispose=dlsym(RTLD_NEXT,"AudioComponentInstanceDispose");
 next_property=dlsym(RTLD_NEXT,"AudioUnitGetProperty");
 next_set_property=dlsym(RTLD_NEXT,"AudioUnitSetProperty");
 device_clock=dlsym(RTLD_DEFAULT,"AudioDeviceGetCurrentTime");
#else
 /* Mach-O interposition excludes the interposing image's own references
  * (dyld's neverImage rule). Bind these directly: resolving AudioUnit symbols
  * lazily inside an intercepted property call can reenter framework setup.
  * Do not stack this observer with another AudioUnit interposer. */
 next_start=AudioOutputUnitStart;next_stop=AudioOutputUnitStop;
 next_uninitialize=AudioUnitUninitialize;next_dispose=AudioComponentInstanceDispose;
 next_property=AudioUnitGetProperty;next_set_property=AudioUnitSetProperty;
 device_clock=AudioDeviceGetCurrentTime;
#endif
 const char *value=getenv("SF_FIREFOX_MEDIA_TRACE");enabled=value&&!strcmp(value,"1");
 const char *limit=getenv("SF_FIREFOX_MEDIA_TRACE_SECONDS");
 if(limit){char *end;long seconds=strtol(limit,&end,10);if(*limit&&!*end&&seconds>=10&&seconds<=180)duration=seconds;}
}
static const char *provider(unit_operation operation){
 Dl_info info={0};return operation&&dladdr((void *)operation,&info)&&info.dli_fname?info.dli_fname:"unresolved";
}
static void *observe(void *unused){
 (void)unused;
 /* Optional PD extension lookup is outside API initialization/once locks. */
 connected_session=dlsym(RTLD_DEFAULT,"SFAudioSessionIfConnected");
 double deadline=clock_seconds(CLOCK_MONOTONIC)+duration;
 for(;;){
  double now=clock_seconds(CLOCK_MONOTONIC);
  AudioDeviceID devices[MAX_UNITS];unsigned active[MAX_UNITS]={0},count=0;
  struct {AudioUnit unit;unsigned index,active;uint64_t generation;} selected[MAX_UNITS];unsigned selected_count=0;
  unsigned installed[MAX_CALLBACKS]={0},observed_callbacks=0;
  pthread_mutex_lock(&lock);
  if(now>=deadline){worker_running=0;pthread_mutex_unlock(&lock);break;}
  for(unsigned i=0;i<MAX_UNITS;i++){
   if(!units[i].device||(!units[i].active&&now-units[i].changed>5))continue;
   unsigned n=0;while(n<count&&devices[n]!=units[i].device)n++;
   if(n==count)devices[count++]=units[i].device;
   active[n]+=units[i].active;
   for(unsigned cb=callback_count;cb>0;cb--)if(callbacks[cb-1].installed&&callbacks[cb-1].unit==units[i].token){
    selected[selected_count].unit=units[i].token;selected[selected_count].index=cb-1;
    selected[selected_count].active=units[i].active;selected[selected_count++].generation=units[i].generation;break;
   }
  }
  observed_callbacks=callback_count;
  for(unsigned i=0;i<observed_callbacks;i++)installed[i]=callbacks[i].installed;
  pthread_mutex_unlock(&lock);
  for(unsigned i=0;i<selected_count;i++){
   uint64_t calls=0,frames=0,successes=0,errors=0,last=0;unsigned registrations=0;
   for(unsigned n=0;n<observed_callbacks;n++)if(installed[n]&&callbacks[n].unit==selected[i].unit){
    struct callback_state *cb=&callbacks[n];registrations++;
    calls+=__atomic_load_n(&cb->calls,__ATOMIC_ACQUIRE);frames+=__atomic_load_n(&cb->frames,__ATOMIC_ACQUIRE);
    successes+=__atomic_load_n(&cb->successes,__ATOMIC_ACQUIRE);errors+=__atomic_load_n(&cb->errors,__ATOMIC_ACQUIRE);
    uint64_t host=__atomic_load_n(&cb->last_host,__ATOMIC_ACQUIRE);if(host>last)last=host;
   }
   /* Sum every registration at this unit address, so an old callback still
    * running after replacement is not hidden by observing only the latest. */
   fprintf(stderr,"SFMEDIA_CALLBACK pid=%d unit=%p generation=%llu registrations=%u monotonic=%.6f calls=%llu requested-frames=%llu successes=%llu errors=%llu last-host=%llu active=%u note=all-registrations-at-unit-address-not-delivery\n",
    getpid(),(void *)selected[i].unit,(unsigned long long)selected[i].generation,registrations,clock_seconds(CLOCK_MONOTONIC),
    (unsigned long long)calls,(unsigned long long)frames,(unsigned long long)successes,(unsigned long long)errors,(unsigned long long)last,selected[i].active);
  }
  for(unsigned i=0;i<count;i++){
   AudioTimeStamp stamp={0};OSStatus status=device_clock?device_clock(devices[i],&stamp):-4;
   fprintf(stderr,"FXHAL pid=%d device=%u monotonic=%.6f epoch=%.6f status=%d consumed=%.0f host=%llu flags=%x active-tracked-units=%u note=PD-per-process-device-HAL-not-acoustic-or-per-unit\n",
    getpid(),(unsigned)devices[i],clock_seconds(CLOCK_MONOTONIC),clock_seconds(CLOCK_REALTIME),(int)status,
    stamp.mSampleTime,(unsigned long long)stamp.mHostTime,(unsigned)stamp.mFlags,active[i]);
   /* PD's existing session table and mapping live until process exit. This
    * getter does not connect a device. Version-check before reading the
    * remaining protocol fields; never use this extension on native macOS.
    * Unlike GetCurrentTime it remains useful after the IOProc stops. */
   const struct sf_audio_session *session=connected_session?connected_session(devices[i]):NULL;
   const struct sf_audio_ring *ring=session?session->ring:NULL;
   if(ring&&ring->version==kSFAudioRingVersion&&ring->channels>0&&ring->channels<=16&&
      ring->periodFrames>0&&ring->periodFrames<=ring->frames&&ring->frames<=(1u<<20)&&
      ring->sampleRate>=8000&&ring->sampleRate<=384000){
    uint64_t read=__atomic_load_n(&ring->readFrame,__ATOMIC_ACQUIRE);
    uint64_t written=__atomic_load_n(&ring->writeFrame,__ATOMIC_ACQUIRE);
    fprintf(stderr,"SFMEDIA_RING pid=%d device=%u monotonic=%.6f read=%llu write=%llu running=%u rate=%u period=%u capacity=%u read-host=%llu active-tracked-units=%u protocol=%u ring-address=%p note=own-process-read-only-ring-not-acoustic\n",
     getpid(),(unsigned)devices[i],clock_seconds(CLOCK_MONOTONIC),(unsigned long long)read,(unsigned long long)written,
     __atomic_load_n(&ring->running,__ATOMIC_ACQUIRE),ring->sampleRate,ring->periodFrames,ring->frames,
     (unsigned long long)__atomic_load_n(&ring->readHostTime,__ATOMIC_ACQUIRE),active[i],ring->version,(void *)ring);
   }else fprintf(stderr,"SFMEDIA_RING pid=%d device=%u unavailable=1 extension=%u\n",getpid(),(unsigned)devices[i],connected_session!=NULL);
  }
  usleep(250000);
 }
 return NULL;
}
static void state_change(const char *event,AudioUnit unit,AudioDeviceID device,OSStatus status){
 double now=clock_seconds(CLOCK_MONOTONIC);pthread_mutex_lock(&lock);
 unsigned index=0;while(index<MAX_UNITS&&units[index].token!=unit)index++;
 if(index==MAX_UNITS){
  for(index=0;index<MAX_UNITS;index++)if(!units[index].token||(units[index].disposed&&now-units[index].changed>5))break;
  if(index<MAX_UNITS)units[index]=(struct unit_state){.token=unit,.generation=++generation};
 }
 if(index<MAX_UNITS&&status==0){
  struct unit_state *s=&units[index];
  if(!strcmp(event,"start")){
   if(s->disposed){s->generation=++generation;s->disposed=0;}
   s->active=1;if(device)s->device=device;
  }else{s->active=0;if(!strcmp(event,"dispose"))s->disposed=1;}
  s->changed=now;
 }
 fprintf(stderr,"SFMEDIA_AU phase=state pid=%d event=%s unit=%p generation=%llu tracked=%u device=%u active=%u disposed=%u status=%d monotonic=%.6f\n",
  getpid(),event,(void *)unit,(unsigned long long)(index<MAX_UNITS?units[index].generation:0),index<MAX_UNITS,
  (unsigned)(index<MAX_UNITS?units[index].device:device),index<MAX_UNITS?units[index].active:0,index<MAX_UNITS?units[index].disposed:0,(int)status,now);
 if(!strcmp(event,"start")&&status==0&&!worker_running){
  pthread_t thread;int error=pthread_create(&thread,NULL,observe,NULL);
  if(!error){worker_running=1;pthread_detach(thread);}
  else fprintf(stderr,"SFMEDIA_AU observer-thread-error=%d pid=%d\n",error,getpid());
 }
 pthread_mutex_unlock(&lock);
}
static OSStatus operation(const char *event,AudioUnit unit,unit_operation next){
 AudioDeviceID device=0;UInt32 size=sizeof(device);OSStatus property=-4;
 uint64_t seq=__atomic_add_fetch(&sequence,1,__ATOMIC_RELAXED);
 double start=clock_seconds(CLOCK_MONOTONIC);
 if(enabled){
  if(!strcmp(event,"start")&&next_property)
   property=next_property(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&device,&size);
  fprintf(stderr,"SFMEDIA_AU phase=begin pid=%d seq=%llu event=%s unit=%p provider=%s monotonic=%.6f device-query=%d device=%u\n",
   getpid(),(unsigned long long)seq,event,(void *)unit,provider(next),start,(int)property,(unsigned)device);
 }
 double call_start=clock_seconds(CLOCK_MONOTONIC);
 OSStatus status=next?next(unit):-4;
 double call_end=clock_seconds(CLOCK_MONOTONIC);
 if(enabled){
  fprintf(stderr,"SFMEDIA_AU phase=end pid=%d seq=%llu event=%s unit=%p status=%d duration-us=%.0f monotonic=%.6f\n",
   getpid(),(unsigned long long)seq,event,(void *)unit,(int)status,(call_end-call_start)*1e6,call_end);
  state_change(event,unit,property==0&&size==sizeof(device)?device:0,status);
 }
 return status;
}
static OSStatus traced_start(AudioUnit unit){pthread_once(&once,resolve);return operation("start",unit,next_start);}
static OSStatus traced_stop(AudioUnit unit){pthread_once(&once,resolve);return operation("stop",unit,next_stop);}
static OSStatus traced_uninitialize(AudioUnit unit){pthread_once(&once,resolve);return operation("uninitialize",unit,next_uninitialize);}
static OSStatus traced_dispose(AudioUnit unit){pthread_once(&once,resolve);return operation("dispose",unit,next_dispose);}
static OSStatus callback_proxy(void *opaque,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *stamp,
                               UInt32 bus,UInt32 frames,AudioBufferList *data){
 struct callback_state *cb=opaque;
 __atomic_fetch_add(&cb->calls,1,__ATOMIC_RELAXED);__atomic_fetch_add(&cb->frames,frames,__ATOMIC_RELAXED);
 __atomic_store_n(&cb->last_host,mach_absolute_time(),__ATOMIC_RELEASE);
 OSStatus status=cb->original.inputProc(cb->original.inputProcRefCon,flags,stamp,bus,frames,data);
 if(status==0)__atomic_fetch_add(&cb->successes,1,__ATOMIC_RELEASE);
 else __atomic_fetch_add(&cb->errors,1,__ATOMIC_RELEASE);
 return status;
}
static OSStatus traced_set_property(AudioUnit unit,AudioUnitPropertyID property,AudioUnitScope scope,AudioUnitElement element,const void *value,UInt32 size){
 pthread_once(&once,resolve);
 if(!next_set_property)return -4;
 if(!enabled||property!=kAudioUnitProperty_SetRenderCallback||element!=0||!value||size!=sizeof(AURenderCallbackStruct))
  return next_set_property(unit,property,scope,element,value,size);
 AURenderCallbackStruct original;memcpy(&original,value,sizeof(original));
 if(!original.inputProc)return next_set_property(unit,property,scope,element,value,size);
 pthread_mutex_lock(&lock);
 unsigned index=callback_count;
 if(index<MAX_CALLBACKS){callback_count++;callbacks[index].unit=unit;callbacks[index].original=original;}
 pthread_mutex_unlock(&lock);
 if(index==MAX_CALLBACKS){
  fprintf(stderr,"SFMEDIA_CALLBACK pid=%d tracking-capacity-exhausted=1\n",getpid());
  return next_set_property(unit,property,scope,element,value,size);
 }
 AURenderCallbackStruct wrapped={callback_proxy,&callbacks[index]};
 OSStatus status=next_set_property(unit,property,scope,element,&wrapped,sizeof(wrapped));
 pthread_mutex_lock(&lock);callbacks[index].installed=status==0;pthread_mutex_unlock(&lock);
 fprintf(stderr,"SFMEDIA_CALLBACK_REGISTER pid=%d unit=%p registration=%u status=%d\n",getpid(),(void *)unit,index,(int)status);
 return status;
}
static OSStatus traced_get_property(AudioUnit unit,AudioUnitPropertyID property,AudioUnitScope scope,AudioUnitElement element,void *value,UInt32 *size){
 pthread_once(&once,resolve);
 OSStatus status=next_property?next_property(unit,property,scope,element,value,size):-4;
 if(enabled&&!status&&property==kAudioUnitProperty_SetRenderCallback&&value&&size&&*size==sizeof(AURenderCallbackStruct)){
  AURenderCallbackStruct returned;memcpy(&returned,value,sizeof(returned));
  uintptr_t address=(uintptr_t)returned.inputProcRefCon,base=(uintptr_t)callbacks;
  if(returned.inputProc==callback_proxy&&address>=base&&address-base<sizeof(callbacks)&&
     (address-base)%sizeof(callbacks[0])==0){
   struct callback_state *cb=(struct callback_state *)address;
   memcpy(value,&cb->original,sizeof(cb->original));
  }
 }
 return status;
}
#ifndef SF_MEDIA_OBSERVER_HOST_TEST
__attribute__((used,section("__DATA,__interpose")))
static const struct{const void *replacement,*original;} interpose[]={
 {(const void *)traced_start,(const void *)AudioOutputUnitStart},
 {(const void *)traced_stop,(const void *)AudioOutputUnitStop},
 {(const void *)traced_uninitialize,(const void *)AudioUnitUninitialize},
 {(const void *)traced_dispose,(const void *)AudioComponentInstanceDispose},
 {(const void *)traced_set_property,(const void *)AudioUnitSetProperty},
 {(const void *)traced_get_property,(const void *)AudioUnitGetProperty},
};
#endif
