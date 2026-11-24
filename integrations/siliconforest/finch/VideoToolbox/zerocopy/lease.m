/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#include "lease.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
extern void IOSurfaceIncrementUseCount(IOSurfaceRef);
extern void IOSurfaceDecrementUseCount(IOSurfaceRef);
extern Boolean IOSurfaceIsInUse(IOSurfaceRef);
struct SFFrameLease {
 AVFrame *frame;IOSurfaceRef surface;struct SFFrameLease *next,*groupNext;
 SFFrameLeaseGroup *group;
 int counted;
};
struct SFFrameLeaseGroup {struct SFFrameLease *head;unsigned references,retired;};
static pthread_mutex_t leaseLock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t leaseWake=PTHREAD_COND_INITIALIZER;
static pthread_once_t leaseOnce=PTHREAD_ONCE_INIT;
static struct SFFrameLease *pending;
static int workerReady;
static uint64_t issued,released,deferred;
static void drop(struct SFFrameLease *lease) {
 /* Group membership is stable until this sole drop; retirement only detaches
  * frame pointers under the lock. The ungrouped/default path is unchanged. */
 if(lease->group){
  pthread_mutex_lock(&leaseLock);
  SFFrameLeaseGroup *group=lease->group;
  struct SFFrameLease **cursor=&group->head;
  while(*cursor&&*cursor!=lease)cursor=&(*cursor)->groupNext;
  if(!*cursor)abort();
  *cursor=lease->groupNext;
  AVFrame *frame=lease->frame;lease->frame=NULL;
  int destroy=!--group->references;
  pthread_mutex_unlock(&leaseLock);
  av_frame_free(&frame);
  if(destroy)free(group);
 }
 int counted=lease->counted;
 av_frame_free(&lease->frame);CFRelease(lease->surface);free(lease);
 if(counted)__atomic_fetch_add(&released,1,__ATOMIC_RELEASE);
}
static void *worker(void *unused) {
 (void)unused;
 for(;;){
  pthread_mutex_lock(&leaseLock);
  while(!pending)pthread_cond_wait(&leaseWake,&leaseLock);
  pthread_mutex_unlock(&leaseLock);
  usleep(5000);
  struct SFFrameLease *ready=NULL;
  pthread_mutex_lock(&leaseLock);
  struct SFFrameLease **cursor=&pending;
  while(*cursor){
   struct SFFrameLease *lease=*cursor;
   /* GetUseCount is process-local. Only IsInUse observes other processes.
    * The consumer must retain its use until GPU completion and establish its
    * hold before the producer drops its own; this predicate is not a fence. */
   if(!IOSurfaceIsInUse(lease->surface)){*cursor=lease->next;lease->next=ready;ready=lease;}
   else cursor=&lease->next;
  }
  pthread_mutex_unlock(&leaseLock);
  while(ready){struct SFFrameLease *next=ready->next;drop(ready);ready=next;}
 }
 return NULL;
}
static void startWorker(void){pthread_t thread;if(!pthread_create(&thread,NULL,worker,NULL)){pthread_detach(thread);workerReady=1;}}
@interface SFVTCaptureFrameLease:NSObject {struct SFFrameLease *_lease;}
- (id)initWithLease:(struct SFFrameLease *)lease;
@end
@implementation SFVTCaptureFrameLease
- (id)initWithLease:(struct SFFrameLease *)lease {if((self=[super init]))_lease=lease;return self;}
- (void)dealloc {
 struct SFFrameLease *lease=_lease;
 if(lease){
  IOSurfaceDecrementUseCount(lease->surface);
  if(!IOSurfaceIsInUse(lease->surface))drop(lease);
  else {
   __atomic_fetch_add(&deferred,1,__ATOMIC_RELAXED);
   pthread_mutex_lock(&leaseLock);lease->next=pending;pending=lease;pthread_cond_signal(&leaseWake);pthread_mutex_unlock(&leaseLock);
  }
 }
 [super dealloc];
}
@end
CFTypeRef SFVTCreateFrameLease(const AVFrame *frame,IOSurfaceRef surface){
 return SFVTCreateGroupedFrameLease(frame,surface,NULL);
}
CFTypeRef SFVTCreateGroupedFrameLease(const AVFrame *frame,IOSurfaceRef surface,SFFrameLeaseGroup *group){
 if(!frame||!frame->buf[0]||!surface)return NULL;
 pthread_once(&leaseOnce,startWorker);if(!workerReady)return NULL;
 struct SFFrameLease *lease=calloc(1,sizeof(*lease));if(!lease)return NULL;
 lease->frame=av_frame_clone(frame);if(!lease->frame){free(lease);return NULL;}
 lease->surface=(IOSurfaceRef)CFRetain(surface);IOSurfaceIncrementUseCount(surface);
 SFVTCaptureFrameLease *object=[[SFVTCaptureFrameLease alloc] initWithLease:lease];
 if(!object){IOSurfaceDecrementUseCount(surface);av_frame_free(&lease->frame);CFRelease(surface);free(lease);return NULL;}
 if(group){
  pthread_mutex_lock(&leaseLock);
  if(group->retired){pthread_mutex_unlock(&leaseLock);CFRelease(object);return NULL;}
  group->references++;lease->group=group;lease->groupNext=group->head;group->head=lease;
  pthread_mutex_unlock(&leaseLock);
 }
 lease->counted=1;__atomic_fetch_add(&issued,1,__ATOMIC_RELAXED);return (CFTypeRef)object;
}
SFFrameLeaseGroup *SFVTFrameLeaseGroupCreate(void){
 SFFrameLeaseGroup *group=calloc(1,sizeof(*group));
 if(group)group->references=1; /* serialized session owner */
 return group;
}
void SFVTFrameLeaseGroupRetire(SFFrameLeaseGroup *group){
 if(!group)return;
 unsigned detached=0;
 pthread_mutex_lock(&leaseLock);group->retired=1;
 for(;;){
  struct SFFrameLease *lease=group->head;
  while(lease&&!lease->frame)lease=lease->groupNext;
  if(!lease)break;
  AVFrame *frame=lease->frame;lease->frame=NULL;detached++;
  /* Freeing the final frame can destroy the real kernel decoder context and
   * release many CF storage tokens. Never run that under the lease list lock. */
  pthread_mutex_unlock(&leaseLock);av_frame_free(&frame);pthread_mutex_lock(&leaseLock);
 }
 int destroy=!--group->references;
 pthread_mutex_unlock(&leaseLock);
 if(destroy)free(group);
 fprintf(stderr,"SF_VT_POOL_RETIRED detached-frames=%u gpu-storage-holds-unchanged=1\n",detached);
}
void SFVTFrameLeaseCounts(uint64_t *a,uint64_t *b,uint64_t *c){
 if(a)*a=__atomic_load_n(&issued,__ATOMIC_ACQUIRE);
 if(b)*b=__atomic_load_n(&released,__ATOMIC_ACQUIRE);
 if(c)*c=__atomic_load_n(&deferred,__ATOMIC_ACQUIRE);
}
