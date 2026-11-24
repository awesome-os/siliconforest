/* SPDX-License-Identifier: MIT */
/* Opt-in timing only. No image reads/copies, waits, pacing or caller changes.
 * Contexts and copied blocks remain stable for this finite diagnostic process.
 * Missing hooks/events are unobserved, never proof of zero cost. */
#include <CoreVideo/CVDisplayLink.h>
#include "VideoToolbox.h"
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RECORDS 32768
#define CONTEXTS 64
extern void *_Block_copy(const void *);
struct event { unsigned ready,kind,context; uint64_t host,duration; int64_t pts; int32_t scale,status; uint32_t flags,plane; uintptr_t token; };
static struct event events[RECORDS];
static unsigned next_event,next_dump,cv_count,vt_count,enabled;
static unsigned seconds=90;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static pthread_mutex_t dump_lock=PTHREAD_MUTEX_INITIALIZER;
struct cv_context { CVDisplayLinkOutputCallback callback; CVDisplayLinkOutputHandler handler; void *data; unsigned index; };
struct vt_context { VTDecompressionOutputCallbackRecord original; unsigned index; VTDecompressionSessionRef session; };
static struct cv_context cvs[CONTEXTS];
static struct vt_context vts[CONTEXTS];
static const char *names[]={"cv-callback","cv-handler","vt-output","cgl-plane","vt-create","cv-register","vt-submit","vt-finish","vt-wait","vt-invalidate"};
static void record(unsigned kind,unsigned context,uint64_t host,uint64_t duration,CMTime pts,int status,unsigned flags,unsigned plane,const void *token)
{
 unsigned index=__atomic_fetch_add(&next_event,1,__ATOMIC_RELAXED);
 if(index>=RECORDS)return;
 struct event *e=&events[index];
 e->kind=kind;e->context=context;e->host=host;e->duration=duration;e->pts=pts.value;e->scale=pts.timescale;e->status=status;e->flags=flags;e->plane=plane;e->token=(uintptr_t)token;
 __atomic_store_n(&e->ready,1,__ATOMIC_RELEASE);
}
static void dump(void)
{
 pthread_mutex_lock(&dump_lock);
 unsigned end=__atomic_load_n(&next_event,__ATOMIC_ACQUIRE);if(end>RECORDS)end=RECORDS;
 // Publication can finish out of reservation order. Never read an incomplete row.
 unsigned batch=0;
 while(next_dump<end&&batch++<1024){
  const struct event *e=&events[next_dump];if(!__atomic_load_n(&e->ready,__ATOMIC_ACQUIRE))break;
  fprintf(stderr,"SFCADENCE pid=%d sequence=%u event=%s context=%u host=%llu duration=%llu pts=%lld scale=%d status=%d flags=%u plane=%u token=%p\n",
   getpid(),next_dump,names[e->kind],e->context,(unsigned long long)e->host,(unsigned long long)e->duration,(long long)e->pts,e->scale,e->status,e->flags,e->plane,(void *)e->token);
  next_dump++;
 }
 pthread_mutex_unlock(&dump_lock);
}
static void *worker(void *unused)
{
 (void)unused;
 for(unsigned i=0;i<seconds*4;i++){usleep(250000);dump();}
 unsigned reserved=__atomic_load_n(&next_event,__ATOMIC_ACQUIRE);
 pthread_mutex_lock(&dump_lock);unsigned dumped=next_dump;pthread_mutex_unlock(&dump_lock);
 fprintf(stderr,"SFCADENCE_END pid=%d reserved=%u capacity=%u dumped=%u lost=%u diagnostic-only=1\n",getpid(),reserved,RECORDS,dumped,reserved>RECORDS?reserved-RECORDS:0);
 return NULL;
}
static void initialize(void)
{
 const char *value=getenv("SF_MEDIA_CADENCE_TRACE");enabled=value&&!strcmp(value,"1");if(!enabled)return;
 value=getenv("SF_MEDIA_CADENCE_SECONDS");if(value){char *end;long n=strtol(value,&end,10);if(*value&&!*end&&n>=5&&n<=180)seconds=(unsigned)n;}
 mach_timebase_info_data_t tb={0};mach_timebase_info(&tb);
 struct timespec mono={0},epoch={0};clock_gettime(CLOCK_MONOTONIC,&mono);clock_gettime(CLOCK_REALTIME,&epoch);
 fprintf(stderr,"SFCADENCE_BEGIN pid=%d numer=%u denom=%u host=%llu monotonic=%lld.%09ld epoch=%lld.%09ld duration-s=%u no-pixel-copy=1 diagnostic-only=1\n",getpid(),tb.numer,tb.denom,(unsigned long long)mach_absolute_time(),(long long)mono.tv_sec,mono.tv_nsec,(long long)epoch.tv_sec,epoch.tv_nsec,seconds);
 pthread_t thread;int error=pthread_create(&thread,NULL,worker,NULL);
 if(!error)pthread_detach(thread);else fprintf(stderr,"SFCADENCE_ERROR pid=%d worker=%d\n",getpid(),error);
}
void SFMediaCadenceFlush(void){if(enabled)dump();}
__attribute__((destructor)) static void finish_dump(void){SFMediaCadenceFlush();}
static CVReturn cv_callback(CVDisplayLinkRef link,const CVTimeStamp *now,const CVTimeStamp *output,CVOptionFlags input,CVOptionFlags *out,void *opaque)
{
 struct cv_context *c=opaque;uint64_t begin=mach_absolute_time();
 CVReturn status=c->callback(link,now,output,input,out,c->data);
 record(0,c->index,begin,mach_absolute_time()-begin,(CMTime){now?now->videoTime:0,now?now->videoTimeScale:0,0,0},status,0,0,link);
 return status;
}
static CVReturn traced_cv_set(CVDisplayLinkRef link,CVDisplayLinkOutputCallback callback,void *data)
{
 pthread_once(&once,initialize);
 if(!enabled||!callback)return CVDisplayLinkSetOutputCallback(link,callback,data);
 unsigned index=__atomic_fetch_add(&cv_count,1,__ATOMIC_RELAXED);
 if(index>=CONTEXTS){fprintf(stderr,"SFCADENCE_ERROR pid=%d cv-capacity=1\n",getpid());return CVDisplayLinkSetOutputCallback(link,callback,data);}
 struct cv_context *c=&cvs[index];*c=(struct cv_context){.callback=callback,.data=data,.index=index};
 CVReturn status=CVDisplayLinkSetOutputCallback(link,cv_callback,c);
 record(5,index,mach_absolute_time(),0,(CMTime){0},status,0,0,link);return status;
}
static void vt_output(void *opaque,void *source,OSStatus status,VTDecodeInfoFlags flags,CVImageBufferRef image,CMTime pts,CMTime duration)
{
 struct vt_context *c=opaque;uint64_t begin=mach_absolute_time();
 c->original.decompressionOutputCallback(c->original.decompressionOutputRefCon,source,status,flags,image,pts,duration);
 record(2,c->index,begin,mach_absolute_time()-begin,pts,status,flags,0,image);
}
static OSStatus traced_vt_create(CFAllocatorRef allocator,CMVideoFormatDescriptionRef format,CFDictionaryRef spec,CFDictionaryRef attributes,
 const VTDecompressionOutputCallbackRecord *callback,VTDecompressionSessionRef *session)
{
 pthread_once(&once,initialize);
 if(!enabled||!callback||!callback->decompressionOutputCallback)return VTDecompressionSessionCreate(allocator,format,spec,attributes,callback,session);
 unsigned index=__atomic_fetch_add(&vt_count,1,__ATOMIC_RELAXED);
 if(index>=CONTEXTS){fprintf(stderr,"SFCADENCE_ERROR pid=%d vt-capacity=1\n",getpid());return VTDecompressionSessionCreate(allocator,format,spec,attributes,callback,session);}
 struct vt_context *c=&vts[index];c->original=*callback;c->index=index;
 VTDecompressionOutputCallbackRecord proxy={vt_output,c};uint64_t begin=mach_absolute_time();
 OSStatus status=VTDecompressionSessionCreate(allocator,format,spec,attributes,&proxy,session);
 if(status==0&&session)__atomic_store_n(&c->session,*session,__ATOMIC_RELEASE);
 record(4,index,begin,mach_absolute_time()-begin,(CMTime){0},status,0,0,status==0&&session?*session:NULL);return status;
}
static unsigned vt_index(VTDecompressionSessionRef session)
{
 if(!session)return UINT32_MAX;
 unsigned count=__atomic_load_n(&vt_count,__ATOMIC_ACQUIRE);if(count>CONTEXTS)count=CONTEXTS;
 for(unsigned i=count;i>0;i--)if(__atomic_load_n(&vts[i-1].session,__ATOMIC_ACQUIRE)==session)return i-1;
 return UINT32_MAX; /* A session not wrapped by this finite observer. */
}
static OSStatus traced_vt_decode(VTDecompressionSessionRef session,CMSampleBufferRef sample,
 VTDecodeFrameFlags flags,void *source,VTDecodeInfoFlags *out)
{
 pthread_once(&once,initialize);
 if(!enabled)return VTDecompressionSessionDecodeFrame(session,sample,flags,source,out);
 uint64_t begin=mach_absolute_time();
 OSStatus status=VTDecompressionSessionDecodeFrame(session,sample,flags,source,out);
 /* Never inspect the input sample, source refcon or caller's output flags. */
 record(6,vt_index(session),begin,mach_absolute_time()-begin,(CMTime){0},status,flags,0,session);return status;
}
static OSStatus traced_vt_finish(VTDecompressionSessionRef session)
{
 pthread_once(&once,initialize);if(!enabled)return VTDecompressionSessionFinishDelayedFrames(session);
 uint64_t begin=mach_absolute_time();OSStatus status=VTDecompressionSessionFinishDelayedFrames(session);
 record(7,vt_index(session),begin,mach_absolute_time()-begin,(CMTime){0},status,0,0,session);return status;
}
static OSStatus traced_vt_wait(VTDecompressionSessionRef session)
{
 pthread_once(&once,initialize);if(!enabled)return VTDecompressionSessionWaitForAsynchronousFrames(session);
 uint64_t begin=mach_absolute_time();OSStatus status=VTDecompressionSessionWaitForAsynchronousFrames(session);
 record(8,vt_index(session),begin,mach_absolute_time()-begin,(CMTime){0},status,0,0,session);return status;
}
static void traced_vt_invalidate(VTDecompressionSessionRef session)
{
 pthread_once(&once,initialize);if(!enabled){VTDecompressionSessionInvalidate(session);return;}
 unsigned index=vt_index(session);uint64_t begin=mach_absolute_time();VTDecompressionSessionInvalidate(session);
 if(index<CONTEXTS)__atomic_store_n(&vts[index].session,NULL,__ATOMIC_RELEASE);
 record(9,index,begin,mach_absolute_time()-begin,(CMTime){0},0,0,0,session);
}
static CGLError traced_plane(CGLContextObj context,GLenum target,GLenum internal,GLsizei width,GLsizei height,GLenum format,GLenum type,IOSurfaceRef surface,GLuint plane)
{
 pthread_once(&once,initialize);
 if(!enabled)return CGLTexImageIOSurface2D(context,target,internal,width,height,format,type,surface,plane);
 uint64_t begin=mach_absolute_time();CGLError status=CGLTexImageIOSurface2D(context,target,internal,width,height,format,type,surface,plane);
 record(3,0,begin,mach_absolute_time()-begin,(CMTime){0},status,0,plane,surface);return status;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} interpose[]={
 {traced_cv_set,CVDisplayLinkSetOutputCallback},
 {traced_vt_create,VTDecompressionSessionCreate},{traced_plane,CGLTexImageIOSurface2D},
 {traced_vt_decode,VTDecompressionSessionDecodeFrame},{traced_vt_finish,VTDecompressionSessionFinishDelayedFrames},
 {traced_vt_wait,VTDecompressionSessionWaitForAsynchronousFrames},{traced_vt_invalidate,VTDecompressionSessionInvalidate}
};
