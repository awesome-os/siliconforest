/* SPDX-License-Identifier: MIT */
/* Own-client opt-in, output-boundary aggregate meter. No PCM recording,
 * mutation, allocation, logging, locks or syscalls on the render thread. */
#include <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "audio-state.h"
enum { CAPACITY=16 };
struct context {
    AudioDeviceIOProc proc;void *data;
    struct sf_pcm_state state;
    /* Atomic publication for the optional background logger. The mutable
     * state above is read/reset directly only after caller quiescence. */
    uint64_t publication[11],sequence;
    unsigned published,valid;
};
static struct context contexts[CAPACITY];
static unsigned count,worker_started;
_Static_assert(sizeof(struct sf_pcm_state)==11*sizeof(uint64_t),"meter publication ABI");
static void publish(struct context *c)
{
    uint64_t words[11];memcpy(words,&c->state,sizeof(words));
    __atomic_fetch_add(&c->sequence,1,__ATOMIC_SEQ_CST);
    for(unsigned i=0;i<11;i++)__atomic_store_n(&c->publication[i],words[i],__ATOMIC_SEQ_CST);
    __atomic_fetch_add(&c->sequence,1,__ATOMIC_SEQ_CST);
}
static int published_state(struct context *c,struct sf_pcm_state *out)
{
    /* Every word is atomic: a seqlock alone on plain C fields is a data race.
     * Bounded retries omit a busy snapshot rather than spin on the HAL. */
    for(unsigned attempt=0;attempt<4;attempt++){
        uint64_t words[11],before=__atomic_load_n(&c->sequence,__ATOMIC_SEQ_CST);
        if(before&1)continue;
        for(unsigned i=0;i<11;i++)words[i]=__atomic_load_n(&c->publication[i],__ATOMIC_SEQ_CST);
        if(before==__atomic_load_n(&c->sequence,__ATOMIC_SEQ_CST)){
            memcpy(out,words,sizeof(words));return 1;
        }
    }
    return 0;
}
static void *worker(void *unused)
{
    (void)unused;struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    double deadline=now.tv_sec+now.tv_nsec/1e9+90;
    for(;;){
        clock_gettime(CLOCK_MONOTONIC,&now);double t=now.tv_sec+now.tv_nsec/1e9;
        struct timespec wall;clock_gettime(CLOCK_REALTIME,&wall);double epoch=wall.tv_sec+wall.tv_nsec/1e9;
        if(t>=deadline)break;
        for(unsigned i=0;i<CAPACITY;i++){
            struct context *c=&contexts[i];struct sf_pcm_state s;
            if(!__atomic_load_n(&c->published,__ATOMIC_ACQUIRE)||!c->valid||!published_state(c,&s))continue;
            fprintf(stderr,"SFHAL_PCM pid=%d context=%u monotonic=%.6f epoch=%.6f calls=%llu frames=%llu samples=%llu square-sum=%.17g peak=%.9g errors=%llu bad-buffers=%llu nonfinite=%llu last-host=%llu\n",
                getpid(),i,t,epoch,(unsigned long long)s.calls,(unsigned long long)s.frames,
                (unsigned long long)s.samples,s.square_sum,s.peak,(unsigned long long)s.errors,
                (unsigned long long)s.bad_buffers,(unsigned long long)s.nonfinite,(unsigned long long)s.last_host);
        }
        usleep(100000);
    }
    return NULL;
}
static OSStatus proxy(AudioObjectID device,const AudioTimeStamp *now,
    const AudioBufferList *input,const AudioTimeStamp *input_time,
    AudioBufferList *output,const AudioTimeStamp *output_time,void *opaque)
{
    struct context *c=opaque;uint64_t host=mach_absolute_time();
    OSStatus result=c->proc(device,now,input,input_time,output,output_time,c->data);
    struct sf_pcm_state *s=&c->state;s->calls++;s->errors+=result!=0;
    if(!s->first_host)s->first_host=host;
    if(s->last_host&&host-s->last_host>s->max_gap)s->max_gap=host-s->last_host;
    s->last_host=host;
    /* Query-validated packed stereo Float32; inspect a bounded single buffer.
     * A malformed/unexpected layout is unobserved, never cast as float. */
    if(!output||output->mNumberBuffers!=1||!output->mBuffers[0].mData||
       output->mBuffers[0].mNumberChannels!=2||output->mBuffers[0].mDataByteSize%8||
       output->mBuffers[0].mDataByteSize>65536){s->bad_buffers++;publish(c);return result;}
    AudioBuffer *b=&output->mBuffers[0];unsigned n=b->mDataByteSize/sizeof(float);
    const float *samples=b->mData;s->frames+=n/2;s->samples+=n;
    for(unsigned i=0;i<n;i++){
        float value=samples[i];uint32_t bits;memcpy(&bits,&value,sizeof(bits));
        if((bits&0x7f800000u)==0x7f800000u){s->nonfinite++;continue;}
        double x=value<0?-(double)value:(double)value;
        s->square_sum+=x*x;if(x>s->peak)s->peak=x;
    }
    publish(c);return result;
}
static OSStatus create(AudioObjectID device,AudioDeviceIOProc proc,void *data,AudioDeviceIOProcID *out)
{
    const char *opt=getenv("SF_HAL_PCM_AUDIT");
    if(!opt)opt=getenv("SFFX_HAL_PCM_AUDIT");
    if(!proc||!opt||strcmp(opt,"1"))return AudioDeviceCreateIOProcID(device,proc,data,out);
    AudioStreamBasicDescription f={0};UInt32 bytes=sizeof(f);
    AudioObjectPropertyAddress a={kAudioDevicePropertyStreamFormat,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyElementMain};
    OSStatus query=AudioObjectGetPropertyData(device,&a,0,NULL,&bytes,&f);
    if(query||bytes!=sizeof(f)||f.mFormatID!=kAudioFormatLinearPCM||
       f.mFormatFlags!=kAudioFormatFlagsNativeFloatPacked||f.mChannelsPerFrame!=2||
       f.mBitsPerChannel!=32||f.mBytesPerFrame!=8||f.mSampleRate<8000||f.mSampleRate>384000)
        return AudioDeviceCreateIOProcID(device,proc,data,out);
    unsigned n=__atomic_fetch_add(&count,1,__ATOMIC_RELAXED);
    if(n>=CAPACITY)return AudioDeviceCreateIOProcID(device,proc,data,out);
    struct context *c=&contexts[n];c->proc=proc;c->data=data;
    OSStatus result=AudioDeviceCreateIOProcID(device,proxy,c,out);c->valid=result==0;
    __atomic_store_n(&c->published,1,__ATOMIC_RELEASE);
    const char *trace=getenv("SF_HAL_PCM_TRACE");unsigned expected=0;
    if(!trace)trace=getenv("SFFX_HAL_PCM_TRACE");
    if(trace&&!strcmp(trace,"1")&&__atomic_compare_exchange_n(&worker_started,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        pthread_t thread;int error=pthread_create(&thread,NULL,worker,NULL);
        if(!error)pthread_detach(thread);
        else fprintf(stderr,"SFHAL_PCM worker-error=%d pid=%d\n",error,getpid());
    }
    return result;
}
int SFHALPCMRead(unsigned index,struct sf_pcm_state *out,int reset)
{
    if(!out||index>=CAPACITY||!__atomic_load_n(&contexts[index].published,__ATOMIC_ACQUIRE)||!contexts[index].valid)return 0;
    *out=contexts[index].state;
    if(reset)memset(&contexts[index].state,0,sizeof(contexts[index].state));
    return 1;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} interpose={create,AudioDeviceCreateIOProcID};
