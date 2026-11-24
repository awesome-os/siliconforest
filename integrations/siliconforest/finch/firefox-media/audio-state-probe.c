/* SPDX-License-Identifier: MIT */
/* M1-only owned quiet streams. Changes no default device/rate/system volume.
 * Measures PCM before HAL mixing and this process's ring, not acoustic output. */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "SFAudioClient.h"
#include "audio-state.h"
struct source { uint64_t frames;double phase,rate; };
static pid_t load_children[4];
static void stop_load(void)
{
    for(unsigned i=0;i<4;i++)if(load_children[i]>0){
        int status;pid_t found=waitpid(load_children[i],&status,WNOHANG);
        /* An unreaped child cannot have its PID reused. Never signal a PID
         * after reaping it or when waitpid does not establish ownership. */
        if(found==0){kill(load_children[i],SIGTERM);waitpid(load_children[i],&status,0);}
    }
}
static void start_load(void)
{
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    uint64_t deadline=mach_absolute_time()+(uint64_t)(8e9*tb.denom/tb.numer);
    if(atexit(stop_load))exit(1);
    for(unsigned i=0;i<4;i++){
        pid_t child=fork();if(child<0)exit(1);
        if(child==0){
            volatile uint64_t value=i+1;
            while(mach_absolute_time()<deadline)
                for(unsigned j=0;j<100000;j++){value^=value<<13;value^=value>>7;value^=value<<17;}
            _exit(0);
        }
        load_children[i]=child;
    }
    puts("AUDIO_STATE_LOAD owned-cpu-children=4 bounded-seconds=8 no-policy-or-affinity-change=1");
}
static void need(int good,const char *reason){if(!good){fprintf(stderr,"AUDIO_STATE_FAIL %s\n",reason);exit(1);}}
static OSStatus render(void *opaque,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *stamp,
    UInt32 bus,UInt32 frames,AudioBufferList *data)
{
    (void)flags;(void)stamp;(void)bus;struct source *s=opaque;
    if(data->mNumberBuffers!=1||data->mBuffers[0].mDataByteSize!=frames*8)return -50;
    float *p=data->mBuffers[0].mData;
    for(UInt32 i=0;i<frames;i++){
        /* Quiet triangle (-60dB peak), independent of callback boundaries. */
        float x=(float)((s->phase<.5?4*s->phase-1:3-4*s->phase)*.001);
        p[2*i]=p[2*i+1]=x;s->phase+=440/s->rate;if(s->phase>=1)s->phase-=1;
    }
    s->frames+=frames;return 0;
}
static void configure(AudioUnit u,struct source *s,double rate)
{
    s->rate=rate;s->frames=0;s->phase=0;
    AudioStreamBasicDescription f={rate,kAudioFormatLinearPCM,kAudioFormatFlagsNativeFloatPacked,8,1,8,2,32,0};
    AURenderCallbackStruct cb={render,s};
    need(AudioUnitSetProperty(u,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&f,sizeof(f))==0,"client format");
    need(AudioUnitSetProperty(u,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&cb,sizeof(cb))==0,"render callback");
    need(AudioUnitInitialize(u)==0,"initialize");
}
static AudioUnit make(AudioDeviceID device,struct source *s,double rate)
{
    AudioComponentDescription d={kAudioUnitType_Output,kAudioUnitSubType_HALOutput,kAudioUnitManufacturer_Apple,0,0};
    AudioComponent c=AudioComponentFindNext(NULL,&d);AudioUnit u=NULL;
    need(c&&AudioComponentInstanceNew(c,&u)==0&&u,"new HAL unit");
    need(AudioUnitSetProperty(u,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&device,sizeof(device))==0,"own device selection");
    configure(u,s,rate);return u;
}
static void volume(AudioUnit u,float value)
{
    need(AudioUnitSetParameter(u,kHALOutputParam_Volume,kAudioUnitScope_Global,0,value,0)==0,"per-unit volume");
    float actual=-1;need(AudioUnitGetParameter(u,kHALOutputParam_Volume,kAudioUnitScope_Global,0,&actual)==0&&actual==value,"volume roundtrip");
}
static struct sf_pcm_state read_state(unsigned index,int reset)
{
    struct sf_pcm_state s={0};need(SFHALPCMRead(index,&s,reset),"registered PCM context");return s;
}
static double report(const char *name,unsigned index,struct source *source,int expect_sound)
{
    if(load_children[0]>0){
        for(unsigned i=0;i<4;i++){
            int status;pid_t found=waitpid(load_children[i],&status,WNOHANG);
            if(found>0)load_children[i]=0;
            need(found==0,"all own load workers still active through phase end");
        }
    }
    struct sf_pcm_state s=read_state(index,1);mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    double power=s.samples?s.square_sum/s.samples:0;
    printf("AUDIO_STATE phase=%s context=%u calls=%llu frames=%llu client-frames=%llu client-rate=%.0f power=%.12g peak=%.9g errors=%llu bad-buffers=%llu nonfinite=%llu max-gap-ms=%.6f\n",name,index,
        (unsigned long long)s.calls,(unsigned long long)s.frames,(unsigned long long)source->frames,source->rate,power,s.peak,
        (unsigned long long)s.errors,(unsigned long long)s.bad_buffers,(unsigned long long)s.nonfinite,s.max_gap*(double)tb.numer/tb.denom/1e6);
    need(s.calls>=10&&s.frames&&s.samples==s.frames*2&&!s.errors&&!s.bad_buffers&&!s.nonfinite,"valid output cycles");
    need(expect_sound?power>1e-9:power==0,"expected nonzero or muted PCM");
    source->frames=0;return power;
}
static void cycle(AudioUnit one,AudioUnit two)
{
    need(AudioOutputUnitStart(one)==0,"start first");
    if(two)need(AudioOutputUnitStart(two)==0,"start second");
    usleep(350000);
    need(AudioOutputUnitStop(one)==0,"stop first");
    if(two)need(AudioOutputUnitStop(two)==0,"stop second");
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    int load=argc==2&&!strcmp(argv[1],"--load");
    if(argc!=1&&!load)return 64;
    if(access("/dev/sfavd",F_OK)){fprintf(stderr,"M1-only owned HAL test; no reference-Mac output\n");return 77;}
    need(setenv("SF_HAL_PCM_AUDIT","1",1)==0,"enable own diagnostic");
    if(load)start_load();
    AudioObjectPropertyAddress a={kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioDeviceID device=0;UInt32 bytes=sizeof(device);
    need(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&bytes,&device)==0&&device,"default output query");
    Float64 rate=0;bytes=sizeof(rate);a.mSelector=kAudioDevicePropertyNominalSampleRate;
    need(AudioObjectGetPropertyData(device,&a,0,NULL,&bytes,&rate)==0&&rate==48000,"48k device test precondition");
    printf("AUDIO_STATE_DEVICE rate=%.0f global-properties-mutated=0\n",rate);
    struct source s[2]={{0}};AudioUnit one=make(device,&s[0],rate);
    volume(one,1);cycle(one,NULL);double full=report("full",0,&s[0],1);
    volume(one,.5);cycle(one,NULL);double half=report("half",0,&s[0],1);
    need(half/full>.23&&half/full<.27,"half amplitude means quarter power");
    volume(one,0);cycle(one,NULL);report("muted",0,&s[0],0);
    struct sf_pcm_state paused=read_state(0,0);usleep(250000);struct sf_pcm_state later=read_state(0,0);
    need(!memcmp(&paused,&later,sizeof(paused)),"no callbacks after stop returns");
    printf("AUDIO_STATE_PAUSE stopped-callbacks-stable=1 interval-ms=250\n");
    AudioUnit two=make(device,&s[1],rate);volume(one,0);volume(two,.5);
    cycle(one,two);report("two-first-muted",0,&s[0],0);report("two-second-half",1,&s[1],1);
    need(AudioOutputUnitStart(two)==0,"restart second alone");usleep(350000);need(AudioOutputUnitStop(two)==0,"stop second alone");
    report("second-survives-first-stop",1,&s[1],1);
    need(read_state(0,0).calls==0,"first remains stopped while second plays");
    need(AudioUnitUninitialize(one)==0,"uninitialize before rate change");
    configure(one,&s[0],44100);volume(one,.5);cycle(one,NULL);
    /* Uninitialize destroys the old IOProc; reinitializing registers a fresh
     * one. Retired diagnostic contexts stay allocated, never get recycled. */
    need(read_state(0,0).calls==0,"retired first IOProc stays stopped");
    report("reinitialized-44100",2,&s[0],1);
    const struct sf_audio_session *(*session)(AudioObjectID)=dlsym(RTLD_DEFAULT,"SFAudioSessionIfConnected");
    const struct sf_audio_session *connected=session?session(device):NULL;
    need(connected&&connected->ring&&connected->ring->version==2&&connected->ring->sampleRate==48000,"own PD ring v2");
    const struct sf_audio_ring *r=connected->ring;uint64_t written=__atomic_load_n(&r->writeFrame,__ATOMIC_ACQUIRE),read=__atomic_load_n(&r->readFrame,__ATOMIC_ACQUIRE);
    need(written&&read,"own ring accepted and consumed frames");
    printf("AUDIO_STATE_RING rate=%u written=%llu read=%llu running=%u acoustic-proof=0\n",r->sampleRate,(unsigned long long)written,(unsigned long long)read,__atomic_load_n(&r->running,__ATOMIC_ACQUIRE));
    need(AudioUnitUninitialize(one)==0&&AudioComponentInstanceDispose(one)==0,"dispose first");
    need(AudioUnitUninitialize(two)==0&&AudioComponentInstanceDispose(two)==0,"dispose second");
    puts("AUDIO_STATE_DONE own-clients-only=1 no-browser-or-acoustic-proof=1");return 0;
}
