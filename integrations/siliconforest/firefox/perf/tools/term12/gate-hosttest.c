#define _GNU_SOURCE
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
typedef signed char BOOL;
#define YES 1
#define NO 0
static int gateOn(void){
   static int on=-1;
   if(on<0){
    const char *v=getenv("SFFX_GPU_RENDER_THREAD"),*gpu=getenv("SFFX_GPU_PRESENT");
    on=v!=NULL&&!strcmp(v,"1")&&gpu!=NULL&&!strcmp(gpu,"1")&&getenv("SF_CA_NO_GATE")==NULL;
   }
   return on;
}
static struct { pthread_mutex_t lock; pthread_cond_t moved; int open,rendering,waiting; } gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER};
static _Thread_local int gateDepth,gateTook,gateRendering;
static int gateFull(void){
   static int full=-1;
   if(full<0){const char *v=getenv("SF_CA_GATE");full=!(v!=NULL&&!strcmp(v,"soft"));}
   return full;
}
static unsigned long gateTimeouts,gateRenders,gateRenderWaits,gateBeginWaits,gateOverlaps;
static double gateRenderWaitMs,gateBeginWaitMs;
static double gateNowMs(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}

static void gateTransactionBegin(void){
   if(!gateOn() || gateDepth++>0 || gateRendering)
    return;
   pthread_mutex_lock(&gate.lock);
   if(gateFull() && (gate.rendering || gate.waiting)){
    double t0=gateNowMs();
    while(gate.rendering || gate.waiting)
     pthread_cond_wait(&gate.moved,&gate.lock);
    gateBeginWaits++;gateBeginWaitMs+=gateNowMs()-t0;
   }
   if(gate.rendering)
    gateOverlaps++;     // a transaction opened while a render reads the tree (soft gate): that frame may show it half done
   gate.open++;
   pthread_mutex_unlock(&gate.lock);
   gateTook=1;
}

static void gateTransactionCommit(void){
   if(!gateOn() || gateDepth==0 || --gateDepth>0 || !gateTook)
    return;
   gateTook=0;
   pthread_mutex_lock(&gate.lock);
   gate.open--;
   pthread_cond_broadcast(&gate.moved);
   pthread_mutex_unlock(&gate.lock);
}

// a render reads the tree: no other thread's transaction is open meanwhile (its own, if any, does not count)
// YES: the render may read the tree (call _CATransactionGateRenderExit after); NO: a transaction stayed open, no render
BOOL _CATransactionGateRenderEnter(void){
   if(!gateOn())
    return YES;
   struct timespec deadline;
   clock_gettime(CLOCK_REALTIME,&deadline);
   deadline.tv_nsec+=20*1000000;
   if(deadline.tv_nsec>=1000000000){deadline.tv_sec++;deadline.tv_nsec-=1000000000;}
   int mine=gateTook;
   pthread_mutex_lock(&gate.lock);
   gate.waiting++;
   gateRenders++;
   BOOL admitted=YES;
   if(gate.open-mine>0){
    double t0=gateNowMs();
    while(gate.open-mine>0)
     if(pthread_cond_timedwait(&gate.moved,&gate.lock,&deadline)!=0 && gate.open-mine>0){
      gateTimeouts++;
      admitted=NO;
      break;
     }
    gateRenderWaits++;gateRenderWaitMs+=gateNowMs()-t0;
   }
   gate.waiting--;
   if(admitted)
    gate.rendering++;
   else
    pthread_cond_broadcast(&gate.moved);   // begins held for the waiting render go on
   pthread_mutex_unlock(&gate.lock);
   if(admitted)
    gateRendering++;
   return admitted;
}

void _CATransactionGateRenderExit(void){
   if(!gateOn())
    return;
   gateRendering--;
   pthread_mutex_lock(&gate.lock);
   gate.rendering--;
   pthread_cond_broadcast(&gate.moved);
   pthread_mutex_unlock(&gate.lock);
}

// (trace) since the last call: renders, renders that waited for transactions (ms), timeouts, begins that waited (ms)
void _CATransactionGateStats(char *line,size_t size){
   pthread_mutex_lock(&gate.lock);
   snprintf(line,size,"gate%s renders=%lu waited=%lu/%.1fms timeouts=%lu begins-waited=%lu/%.1fms overlaps=%lu",gateFull()?"(full)":"",gateRenders,
            gateRenderWaits,gateRenderWaitMs,gateTimeouts,gateBeginWaits,gateBeginWaitMs,gateOverlaps);
   gateRenders=gateRenderWaits=gateTimeouts=gateBeginWaits=gateOverlaps=0;gateRenderWaitMs=gateBeginWaitMs=0;
   pthread_mutex_unlock(&gate.lock);
}


static volatile int writerInside=0, overlap=0;
static void *writerHold(void *ms){ gateTransactionBegin(); writerInside=1; usleep((long)(size_t)ms*1000); writerInside=0; gateTransactionCommit(); return NULL; }
static void *writerLoop(void *n){ for(int i=0;i<(int)(size_t)n;i++){ gateTransactionBegin(); writerInside=1; usleep(300); writerInside=0; gateTransactionCommit(); usleep(200);} return NULL; }
int main(void){
  setenv("SFFX_GPU_RENDER_THREAD","1",1); setenv("SFFX_GPU_PRESENT","1",1);
  int fails=0;
  // 1: a writer held 100 ms: the render is not admitted
  pthread_t t; pthread_create(&t,NULL,writerHold,(void*)100); usleep(5000);
  BOOL a=_CATransactionGateRenderEnter(); if(a){ if(writerInside) {printf("FAIL admitted with writer open\n");fails++;} _CATransactionGateRenderExit(); } else printf("ok 1: render refused while a writer stayed open\n");
  pthread_join(t,NULL);
  // 2: many short writers while rendering: no writer is inside during an admitted render
  pthread_create(&t,NULL,writerLoop,(void*)3000);
  int renders=0,refused=0;
  for(int i=0;i<2000;i++){ if(_CATransactionGateRenderEnter()){ for(int k=0;k<50;k++){ if(writerInside){overlap++;break;} usleep(10);} _CATransactionGateRenderExit(); renders++; } else refused++; }
  pthread_join(t,NULL);
  printf("%s 2: %d renders, %d refused, %d overlaps\n", overlap?"FAIL":"ok",renders,refused,overlap); if(overlap)fails++;
  // 3: after everything: no gate state left
  printf("%s 3: open=%d rendering=%d waiting=%d\n",(gate.open||gate.rendering||gate.waiting)?"FAIL":"ok",gate.open,gate.rendering,gate.waiting);
  return fails;
}
