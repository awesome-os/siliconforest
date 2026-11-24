/* SPDX-License-Identifier: MIT */
/* Query-only by default. --silence exercises only our own AUHAL instances;
 * run that mode on M1 under the lead's owner-input guard and shared locks. */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
static uint64_t callbacks;
static void need(int good,const char *what){if(!good){fprintf(stderr,"AUDIO_PROBE_FAIL %s\n",what);exit(1);}}
static OSStatus render(void *unused,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *stamp,
                       UInt32 bus,UInt32 frames,AudioBufferList *data){
 (void)unused;(void)flags;(void)bus;
 uint64_t n=__atomic_add_fetch(&callbacks,1,__ATOMIC_RELAXED);
 for(UInt32 b=0;b<data->mNumberBuffers;b++)memset(data->mBuffers[b].mData,0,data->mBuffers[b].mDataByteSize);
 if(n<=8)fprintf(stderr,"AUDIO_CALLBACK index=%llu frames=%u host=%llu now=%llu sample=%.0f flags=%x\n",
  (unsigned long long)n,(unsigned)frames,(unsigned long long)stamp->mHostTime,(unsigned long long)mach_absolute_time(),stamp->mSampleTime,(unsigned)stamp->mFlags);
 return noErr;
}
static void property32(AudioObjectID object,AudioObjectPropertySelector selector,AudioObjectPropertyScope scope,const char *name){
 AudioObjectPropertyAddress a={selector,scope,kAudioObjectPropertyElementMain};UInt32 value=0,size=sizeof(value);
 OSStatus status=AudioObjectGetPropertyData(object,&a,0,NULL,&size,&value);
 printf("AUDIO_PROPERTY name=%s status=%d bytes=%u frames=%u\n",name,(int)status,(unsigned)size,(unsigned)value);
}
static void latency(AudioUnit unit,AudioUnitPropertyID property,const char *name){
 Float64 value=0;UInt32 size=sizeof(value);Boolean writable=0;UInt32 expected=0;
 OSStatus info=AudioUnitGetPropertyInfo(unit,property,kAudioUnitScope_Global,0,&expected,&writable);
 OSStatus status=AudioUnitGetProperty(unit,property,kAudioUnitScope_Global,0,&value,&size);
 printf("AUDIO_UNIT_PROPERTY name=%s info=%d get=%d bytes=%u info-bytes=%u writable=%u seconds=%.17g\n",
  name,(int)info,(int)status,(unsigned)size,(unsigned)expected,(unsigned)writable,value);
}
static AudioUnit make_unit(AudioDeviceID device,double rate,int initialize){
 AudioComponentDescription desc={kAudioUnitType_Output,kAudioUnitSubType_HALOutput,kAudioUnitManufacturer_Apple,0,0};
 AudioComponent component=AudioComponentFindNext(NULL,&desc);AudioUnit unit=NULL;
 need(component&&AudioComponentInstanceNew(component,&unit)==0&&unit,"new output unit");
 need(AudioUnitSetProperty(unit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&device,sizeof(device))==0,"select own default output");
 if(initialize){
  AudioStreamBasicDescription format={rate,kAudioFormatLinearPCM,kAudioFormatFlagsNativeFloatPacked,8,1,8,2,32,0};
  AURenderCallbackStruct callback={render,NULL};
  need(AudioUnitSetProperty(unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format))==0,"set own stereo float format");
  need(AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof(callback))==0,"set own silent callback");
  AURenderCallbackStruct observed={0};UInt32 bytes=sizeof(observed);
  need(AudioUnitGetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&observed,&bytes)==0&&
       bytes==sizeof(observed)&&observed.inputProc==callback.inputProc&&observed.inputProcRefCon==callback.inputProcRefCon,"callback getter remains transparent");
  need(AudioUnitInitialize(unit)==0,"initialize own output unit");
 }
 return unit;
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IOLBF,0);
 int silence=argc==2&&!strcmp(argv[1],"--silence");
 if(argc!=1&&!silence)return 64;
 if(silence&&access("/dev/sfavd",F_OK)){fprintf(stderr,"M1-only silence lifecycle; never run this mode on reference Mac\n");return 77;}
 AudioObjectPropertyAddress address={kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
 AudioDeviceID device=0;UInt32 size=sizeof(device);
 need(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,&device)==0&&device,"query default output");
 Float64 rate=0;size=sizeof(rate);address.mSelector=kAudioDevicePropertyNominalSampleRate;
 need(AudioObjectGetPropertyData(device,&address,0,NULL,&size,&rate)==0&&rate>=8000&&rate<=384000,"query device rate");
 printf("AUDIO_DEFAULT available=1 rate=%.17g mode=%s\n",rate,silence?"own-silence":"query-only-no-initialize-or-start");
 property32(device,kAudioDevicePropertyLatency,kAudioObjectPropertyScopeOutput,"device-latency");
 property32(device,kAudioDevicePropertySafetyOffset,kAudioObjectPropertyScopeOutput,"safety-offset");
 property32(device,kAudioDevicePropertyBufferFrameSize,kAudioObjectPropertyScopeGlobal,"buffer-frames");
 AudioStreamID streams[32]={0};size=sizeof(streams);address.mSelector=kAudioDevicePropertyStreams;address.mScope=kAudioObjectPropertyScopeOutput;
 OSStatus streamStatus=AudioObjectGetPropertyData(device,&address,0,NULL,&size,streams);
 printf("AUDIO_STREAMS status=%d bytes=%u\n",(int)streamStatus,(unsigned)size);
 if(!streamStatus&&size>=sizeof(AudioStreamID)&&size<=sizeof(streams))property32(streams[0],kAudioStreamPropertyLatency,kAudioObjectPropertyScopeGlobal,"first-stream-latency");
 AudioUnit one=make_unit(device,rate,silence);
 latency(one,kAudioUnitProperty_Latency,"unit-latency");
 latency(one,40,"presentation-latency"); /* Public property40, own declaration. */
 if(silence){
  need(AudioOutputUnitStart(one)==0,"start own first unit");usleep(350000);
  need(AudioOutputUnitStop(one)==0,"stop own first unit");usleep(750000);
  need(AudioOutputUnitStart(one)==0,"restart own first unit");
  AudioUnit two=make_unit(device,rate,1);need(AudioOutputUnitStart(two)==0,"start own second unit");usleep(350000);
  need(AudioOutputUnitStop(one)==0,"stop one while another own unit plays silence");usleep(350000);
  need(AudioOutputUnitStop(two)==0,"stop own second unit");
  need(AudioUnitUninitialize(two)==0&&AudioComponentInstanceDispose(two)==0,"dispose own second unit");
  usleep(750000);need(AudioUnitUninitialize(one)==0,"uninitialize own first unit");
 }
 need(AudioComponentInstanceDispose(one)==0,"dispose own first unit");
 if(silence)usleep(750000);
 printf("AUDIO_PROBE_DONE callbacks=%llu no-acoustic-or-browser-sync-claim=1\n",(unsigned long long)__atomic_load_n(&callbacks,__ATOMIC_ACQUIRE));
 return 0;
}
