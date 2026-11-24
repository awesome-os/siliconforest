/* SPDX-License-Identifier: MIT
 * Transparent AU start observation. The clock is this process's actual HAL
 * consumer readFrame, not a different probe process or acoustic evidence.
 */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static OSStatus (*real_start)(AudioUnit);
static OSStatus (*get_property)(AudioUnit,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,void *,UInt32 *);
static OSStatus (*get_clock)(AudioObjectID,AudioTimeStamp *);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static unsigned observing;
static void resolve(void){
 real_start=dlsym(RTLD_NEXT,"AudioOutputUnitStart");
 get_property=dlsym(RTLD_NEXT,"AudioUnitGetProperty");
 get_clock=dlsym(RTLD_DEFAULT,"AudioDeviceGetCurrentTime");
}
static double wall(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static double epoch(void){struct timespec t;clock_gettime(CLOCK_REALTIME,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void *observe(void *arg){
 AudioObjectID device=(AudioObjectID)(uintptr_t)arg;
 double end=wall()+125;
 unsigned unavailable=0;
 while(wall()<end){
  AudioTimeStamp stamp={0};OSStatus status=get_clock?get_clock(device,&stamp):-4;
  fprintf(stderr,"FXHAL pid=%d device=%u monotonic=%.6f epoch=%.6f status=%d consumed=%.0f host=%llu flags=%x note=per-process-HAL-not-acoustic\n",
   getpid(),(unsigned)device,wall(),epoch(),(int)status,stamp.mSampleTime,(unsigned long long)stamp.mHostTime,(unsigned)stamp.mFlags);
  if(status){if(++unavailable>=5)break;}else unavailable=0;
  usleep(250000);
 }
 __atomic_store_n(&observing,0,__ATOMIC_RELEASE);return NULL;
}
static OSStatus traced_start(AudioUnit unit){
 pthread_once(&once,resolve);
 OSStatus status=real_start?real_start(unit):-4;
 AudioObjectID device=0;UInt32 size=sizeof(device);
 OSStatus property=status?status:get_property?get_property(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&device,&size):-4;
 Dl_info provider={0};if(real_start)dladdr((void *)real_start,&provider);
 fprintf(stderr,"FXHAL start pid=%d unit=%p status=%d property_status=%d device=%u provider=%s\n",getpid(),unit,(int)status,(int)property,(unsigned)device,provider.dli_fname?provider.dli_fname:"missing");
 unsigned expected=0;
 if(!status&&!property&&device&&get_clock&&__atomic_compare_exchange_n(&observing,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  pthread_t thread;int error=pthread_create(&thread,NULL,observe,(void *)(uintptr_t)device);
  if(!error)pthread_detach(thread);else __atomic_store_n(&observing,0,__ATOMIC_RELEASE);
 }
 return status;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct{const void *replacement,*original;} interpose={
 (const void *)traced_start,(const void *)AudioOutputUnitStart
};
