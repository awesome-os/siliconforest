/* SPDX-License-Identifier: MIT */
/* Public AUHAL API, own PCM callback, real per-process HAL readFrame clock. */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
static uint64_t calls,produced;
static OSStatus render(void *unused,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *time,
                       UInt32 bus,UInt32 frames,AudioBufferList *data) {
 (void)unused;(void)flags;(void)time;(void)bus;
 uint64_t start=__atomic_load_n(&produced,__ATOMIC_RELAXED);
 for(UInt32 b=0;b<data->mNumberBuffers;b++) {
  float *samples=data->mBuffers[b].mData;UInt32 count=data->mBuffers[b].mDataByteSize/sizeof(float);
  for(UInt32 i=0;i<count;i++)samples[i]=((i+start)%200<100?0.01f:-0.01f);
 }
 __atomic_fetch_add(&produced,frames,__ATOMIC_RELEASE);__atomic_fetch_add(&calls,1,__ATOMIC_RELEASE);return noErr;
}
static void need(int condition,const char *message){if(!condition){fprintf(stderr,"AU_DEVICE_FAIL %s\n",message);exit(1);}}
int main(void) {
 AudioObjectPropertyAddress address={kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
 AudioDeviceID device=0;UInt32 bytes=sizeof(device);
 need(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&bytes,&device)==0&&device,"default device");
 AudioComponentDescription desc={kAudioUnitType_Output,kAudioUnitSubType_HALOutput,kAudioUnitManufacturer_Apple,0,0};
 AudioComponent component=AudioComponentFindNext(NULL,&desc);AudioUnit unit=NULL;
 need(component&&AudioComponentInstanceNew(component,&unit)==0&&unit,"HAL component");
 AudioDeviceID invalid=0xfffffff0;
 need(AudioUnitSetProperty(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&invalid,sizeof(invalid))!=0,"reject unknown device");
 need(AudioUnitSetProperty(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&device,sizeof(device))==0,"select actual default ID");
 AudioDeviceID selected=0;bytes=sizeof(selected);
 need(AudioUnitGetProperty(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&selected,&bytes)==0&&selected==device,"device identity retained");
 AudioStreamBasicDescription pcm={48000,kAudioFormatLinearPCM,kAudioFormatFlagsNativeFloatPacked,4,1,4,1,32,0};
 need(AudioUnitSetProperty(unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&pcm,sizeof(pcm))==0,"mono float input");
 AURenderCallbackStruct callback={render,NULL};
 need(AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof(callback))==0,"render callback");
 need(AudioUnitInitialize(unit)==0,"initialize");
 need(AudioOutputUnitStart(unit)==0,"start");
 AudioTimeStamp stamp={0};OSStatus clock=-1;
 for(unsigned i=0;i<200;i++) {
  usleep(10000);clock=AudioDeviceGetCurrentTime(device,&stamp);
  if(clock==0&&stamp.mSampleTime>=12000&&__atomic_load_n(&produced,__ATOMIC_ACQUIRE)>=12000)break;
 }
 OSStatus stop=AudioOutputUnitStop(unit),uninit=AudioUnitUninitialize(unit),dispose=AudioComponentInstanceDispose(unit);
 printf("AU_DEVICE selected-default=1 rejected-unknown=1 callbacks=%llu produced=%llu hal-read-status=%d hal-read-frames=%.0f stop=%d uninit=%d dispose=%d\n",(unsigned long long)calls,(unsigned long long)produced,(int)clock,stamp.mSampleTime,(int)stop,(int)uninit,(int)dispose);
 need(calls&&produced>=12000&&clock==0&&stamp.mSampleTime>=12000&&!stop&&!uninit&&!dispose,"HAL consumed own ring, lifecycle");return 0;
}
