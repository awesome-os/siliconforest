/* SPDX-License-Identifier: MIT */
/* Opt-in Root HAL readFrame observation in this process, not acoustic evidence. */
#include <CoreAudio/CoreAudio.h>
#include <dlfcn.h>
#include <stdio.h>
static pthread_t sf_hal_thread;
static int sf_hal_started,sf_hal_stop;
static void *sf_hal_observe(void *unused) {
 (void)unused;uint64_t maximum=0;unsigned samples=0;
 OSStatus (*get)(AudioObjectID,const AudioObjectPropertyAddress*,UInt32,const void*,UInt32*,void*)=dlsym(RTLD_DEFAULT,"AudioObjectGetPropertyData");
 OSStatus (*clock)(AudioDeviceID,AudioTimeStamp*)=dlsym(RTLD_DEFAULT,"AudioDeviceGetCurrentTime");
 AudioDeviceID device=0;UInt32 bytes=sizeof(device);
 AudioObjectPropertyAddress address={kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
 if(get&&clock&&!get(kAudioObjectSystemObject,&address,0,NULL,&bytes,&device)) {
  while(!__atomic_load_n(&sf_hal_stop,__ATOMIC_ACQUIRE)) {
   AudioTimeStamp stamp={0};
   if(!clock(device,&stamp)&&stamp.mSampleTime>=0&&stamp.mSampleTime<1e12) {
    uint64_t value=(uint64_t)stamp.mSampleTime;if(value>maximum)maximum=value;samples++;
    if(samples%50==0)fprintf(stderr,"SF_VLC_HAL_SAMPLE samples=%u consumed-frames=%llu device=%u\n",samples,(unsigned long long)value,(unsigned)device);
   }
   usleep(20000);
  }
 }
 fprintf(stderr,"SF_VLC_HAL clock-samples=%u max-consumed-frames=%llu device=%u note=Root-per-process-readFrame-not-acoustic\n",samples,(unsigned long long)maximum,(unsigned)device);
 return NULL;
}
static void sf_hal_begin(void) {
 const char *value=getenv("SF_VLC_HAL_LOG");
 if(value&&value[0]=='1'&&!value[1]&&!sf_hal_started){__atomic_store_n(&sf_hal_stop,0,__ATOMIC_RELEASE);sf_hal_started=pthread_create(&sf_hal_thread,NULL,sf_hal_observe,NULL)==0;}
}
static void sf_hal_end(void) {
 if(sf_hal_started){__atomic_store_n(&sf_hal_stop,1,__ATOMIC_RELEASE);pthread_join(sf_hal_thread,NULL);sf_hal_started=0;}
}
