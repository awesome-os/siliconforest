/* SPDX-License-Identifier: MIT */
/* Keep every queue operation in the retained PD image. No private object is
   passed to Finch's queue or converter implementation. dlopen/dlsym are bounded
   to that real provider; an unavailable provider returns a real failure. */
#include <AudioToolbox/AudioQueue.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
static void *provider;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void load_provider(void) {
    Dl_info image;
    if (!dladdr((void *)&load_provider,&image) || !image.dli_fname) return;
    const char *slash=strrchr(image.dli_fname,'/'); if(!slash)return;
    size_t directory=(size_t)(slash-image.dli_fname)+1;
    const char filename[]="AudioToolbox.output-sf";
    char *path=malloc(directory+sizeof(filename));if(!path)return;
    memcpy(path,image.dli_fname,directory);memcpy(path+directory,filename,sizeof(filename));
    provider=dlopen(path,RTLD_NOW|RTLD_LOCAL);free(path);
}
#define FORWARD(name,args,values) OSStatus name args { \
    pthread_once(&once,load_provider); \
    OSStatus (*fn) args = provider ? (OSStatus (*) args)dlsym(provider,#name) : NULL; \
    return fn && (void *)fn != (void *)name ? fn values : kAudioQueueErr_InvalidDevice; }
FORWARD(AudioQueueNewOutput,(const AudioStreamBasicDescription *f,AudioQueueOutputCallback c,void *u,CFRunLoopRef r,CFStringRef m,UInt32 flags,AudioQueueRef *q),(f,c,u,r,m,flags,q))
FORWARD(AudioQueueNewInput,(const AudioStreamBasicDescription *f,AudioQueueInputCallback c,void *u,CFRunLoopRef r,CFStringRef m,UInt32 flags,AudioQueueRef *q),(f,c,u,r,m,flags,q))
FORWARD(AudioQueueAllocateBuffer,(AudioQueueRef q,UInt32 n,AudioQueueBufferRef *b),(q,n,b))
FORWARD(AudioQueueAllocateBufferWithPacketDescriptions,(AudioQueueRef q,UInt32 n,UInt32 p,AudioQueueBufferRef *b),(q,n,p,b))
FORWARD(AudioQueueFreeBuffer,(AudioQueueRef q,AudioQueueBufferRef b),(q,b))
FORWARD(AudioQueueEnqueueBuffer,(AudioQueueRef q,AudioQueueBufferRef b,UInt32 n,const AudioStreamPacketDescription *p),(q,b,n,p))
FORWARD(AudioQueueStart,(AudioQueueRef q,const AudioTimeStamp *t),(q,t))
FORWARD(AudioQueueStop,(AudioQueueRef q,Boolean now),(q,now))
FORWARD(AudioQueuePause,(AudioQueueRef q),(q))
FORWARD(AudioQueueReset,(AudioQueueRef q),(q))
FORWARD(AudioQueueFlush,(AudioQueueRef q),(q))
FORWARD(AudioQueueDispose,(AudioQueueRef q,Boolean now),(q,now))
FORWARD(AudioQueueSetParameter,(AudioQueueRef q,AudioQueueParameterID p,AudioQueueParameterValue v),(q,p,v))
FORWARD(AudioQueueGetParameter,(AudioQueueRef q,AudioQueueParameterID p,AudioQueueParameterValue *v),(q,p,v))
FORWARD(AudioQueueGetProperty,(AudioQueueRef q,AudioQueuePropertyID p,void *v,UInt32 *n),(q,p,v,n))
FORWARD(AudioQueueGetPropertySize,(AudioQueueRef q,AudioQueuePropertyID p,UInt32 *n),(q,p,n))
FORWARD(AudioQueueAddPropertyListener,(AudioQueueRef q,AudioQueuePropertyID p,AudioQueuePropertyListenerProc c,void *u),(q,p,c,u))
FORWARD(AudioQueueRemovePropertyListener,(AudioQueueRef q,AudioQueuePropertyID p,AudioQueuePropertyListenerProc c,void *u),(q,p,c,u))
FORWARD(AudioQueueCreateTimeline,(AudioQueueRef q,AudioQueueTimelineRef *t),(q,t))
FORWARD(AudioQueueDisposeTimeline,(AudioQueueRef q,AudioQueueTimelineRef t),(q,t))
FORWARD(AudioQueueGetCurrentTime,(AudioQueueRef q,AudioQueueTimelineRef t,AudioTimeStamp *s,Boolean *d),(q,t,s,d))

OSStatus AudioQueueSetProperty(AudioQueueRef q,AudioQueuePropertyID property,const void *value,UInt32 size) {
    pthread_once(&once,load_provider);
    OSStatus (*fn)(AudioQueueRef,AudioQueuePropertyID,const void *,UInt32)=provider ? dlsym(provider,"AudioQueueSetProperty") : NULL;
    return fn && (void *)fn!=(void *)AudioQueueSetProperty ? fn(q,property,value,size) : kAudioQueueErr_InvalidProperty;
}
