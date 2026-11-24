/* SPDX-License-Identifier: MIT */
/* Decode before starting output. Queue buffers and all queue operations belong
   to the installed PD provider. A completed callback is HAL consumption evidence,
   not evidence that a listener heard audio or that hardware emitted sound. */
#include <AudioToolbox/AudioQueue.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
struct Player {pthread_mutex_t lock;uint8_t *data;size_t size,next,completed;UInt32 bytesPerFrame;OSStatus error;UInt32 callbacks;};
static void check(OSStatus e,const char *op){if(e){fprintf(stderr,"%s: %d\n",op,(int)e);exit(1);}}
static void fill(struct Player *p,AudioQueueRef q,AudioQueueBufferRef b){
    size_t n=p->size-p->next;if(n>b->mAudioDataBytesCapacity)n=b->mAudioDataBytesCapacity;
    if(!n)return;memcpy(b->mAudioData,p->data+p->next,n);b->mAudioDataByteSize=(UInt32)n;
    OSStatus e=AudioQueueEnqueueBuffer(q,b,0,NULL);if(e)p->error=e;else p->next+=n;
}
static void callback(void *user,AudioQueueRef q,AudioQueueBufferRef b){
    struct Player *p=user;pthread_mutex_lock(&p->lock);p->completed+=b->mAudioDataByteSize;p->callbacks++;fill(p,q,b);pthread_mutex_unlock(&p->lock);
}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IOLBF,0);if(argc!=2)return 2;
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)argv[1],strlen(argv[1]),false);
    ExtAudioFileRef file=NULL;check(ExtAudioFileOpenURL(url,&file),"open");CFRelease(url);
    AudioStreamBasicDescription source;UInt32 n=sizeof(source);check(ExtAudioFileGetProperty(file,kExtAudioFileProperty_FileDataFormat,&n,&source),"format");
    AudioStreamBasicDescription pcm={source.mSampleRate,kAudioFormatLinearPCM,kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,2*source.mChannelsPerFrame,1,2*source.mChannelsPerFrame,source.mChannelsPerFrame,16,0};
    check(ExtAudioFileSetProperty(file,kExtAudioFileProperty_ClientDataFormat,sizeof(pcm),&pcm),"client PCM");
    struct Player p={.lock=PTHREAD_MUTEX_INITIALIZER,.bytesPerFrame=pcm.mBytesPerFrame};
    for(;;){uint8_t block[32768];UInt32 frames=sizeof(block)/pcm.mBytesPerFrame;AudioBufferList list={1,{{pcm.mChannelsPerFrame,sizeof(block),block}}};
        check(ExtAudioFileRead(file,&frames,&list),"decode");if(!frames)break;
        size_t bytes=(size_t)frames*pcm.mBytesPerFrame;if(p.size+bytes>16u*1024u*1024u)return 3;
        void *data=realloc(p.data,p.size+bytes);if(!data)return 4;p.data=data;memcpy(p.data+p.size,block,bytes);p.size+=bytes;
    }
    check(ExtAudioFileDispose(file),"close");if(!p.size)return 5;
    printf("decoded frames=%llu rate=%.0f channels=%u\n",(unsigned long long)(p.size/pcm.mBytesPerFrame),pcm.mSampleRate,(unsigned)pcm.mChannelsPerFrame);
    AudioQueueRef queue=NULL;check(AudioQueueNewOutput(&pcm,callback,&p,NULL,NULL,0,&queue),"AudioQueueNewOutput");
    check(AudioQueueSetParameter(queue,kAudioQueueParam_Volume,0.1f),"queue volume");
    for(int i=0;i<3;i++){AudioQueueBufferRef buffer=NULL;check(AudioQueueAllocateBuffer(queue,1024*pcm.mBytesPerFrame,&buffer),"allocate");fill(&p,queue,buffer);}
    check(AudioQueueStart(queue,NULL),"AudioQueueStart");
    double deadline=seconds()+10;bool complete=false;
    while(seconds()<deadline){pthread_mutex_lock(&p.lock);complete=p.completed==p.size;OSStatus error=p.error;pthread_mutex_unlock(&p.lock);if(error || complete)break;usleep(10000);}
    AudioTimeStamp time={0};Boolean discontinuity=false;OSStatus timeStatus=AudioQueueGetCurrentTime(queue,NULL,&time,&discontinuity);
    check(AudioQueueStop(queue,true),"AudioQueueStop");check(AudioQueueDispose(queue,true),"AudioQueueDispose");
    printf("HAL queued=%llu completed=%llu callbacks=%u converter-error=%d current-time-status=%d sample-time=%.0f\n",(unsigned long long)(p.next/pcm.mBytesPerFrame),(unsigned long long)(p.completed/pcm.mBytesPerFrame),(unsigned)p.callbacks,(int)p.error,(int)timeStatus,time.mSampleTime);
    free(p.data);pthread_mutex_destroy(&p.lock);
    return complete && !p.error && p.callbacks ? 0:6;
}
