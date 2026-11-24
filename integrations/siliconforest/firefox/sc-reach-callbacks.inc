/* SPDX-License-Identifier: MIT */
#include "sc-route-source.h"
#include <CoreFoundation/CFRuntime.h>
#include <pthread.h>
#define SF_REACH_STATE CFSTR("sf-notification-state")
typedef struct Registration {
    struct Registration *next;
    CFRunLoopRef loop; CFStringRef mode; CFRunLoopSourceRef source;
} Registration;
typedef struct {
    unsigned refs; SCNetworkReachabilityCallBack function;
    SCNetworkReachabilityContext context;
} Callback;
static void callbackRelease(Callback *c) {
    if(c&&!__atomic_sub_fetch(&c->refs,1,__ATOMIC_ACQ_REL)){
        if(c->context.release)c->context.release(c->context.info);free(c);
    }
}
typedef struct {
    CFRuntimeBase base; pthread_mutex_t lock;
    Callback *callback;
    Registration *registrations;
    SCNetworkReachabilityFlags flags; Boolean delivered;
} ReachState;
static CFTypeID reachStateType;
static pthread_once_t reachStateOnce=PTHREAD_ONCE_INIT;
static void stateDead(CFTypeRef object) {
    ReachState *s=(ReachState *)object;
    /* A scheduled source retains its target; callers must unschedule it. */
    callbackRelease(s->callback);
    pthread_mutex_destroy(&s->lock);
}
static void stateInit(void) {
    static const CFRuntimeClass c={.version=0,.className="SFReachabilityNotifications",.finalize=stateDead};
    reachStateType=_CFRuntimeRegisterClass(&c);
}
static Boolean sf_reach_state_attach(CFMutableDictionaryRef target) {
    pthread_once(&reachStateOnce,stateInit);if(!reachStateType)return FALSE;
    ReachState *s=(ReachState *)_CFRuntimeCreateInstance(NULL,reachStateType,sizeof(ReachState)-sizeof(CFRuntimeBase),NULL);
    if(!s)return FALSE;
    memset((char *)s+sizeof(CFRuntimeBase),0,sizeof(ReachState)-sizeof(CFRuntimeBase));
    pthread_mutex_init(&s->lock,NULL);
    CFDictionarySetValue(target,SF_REACH_STATE,s);CFRelease(s);return TRUE;
}
static ReachState *reachState(SCNetworkReachabilityRef target) {
    if(!target||CFGetTypeID(target)!=CFDictionaryGetTypeID())return NULL;
    ReachState *s=(ReachState *)CFDictionaryGetValue((CFDictionaryRef)target,SF_REACH_STATE);
    return s&&CFGetTypeID(s)==reachStateType?s:NULL;
}
static void reachChanged(CFTypeRef value,unsigned changes) {
    (void)changes;SCNetworkReachabilityRef target=(SCNetworkReachabilityRef)value;
    ReachState *s=reachState(target);SCNetworkReachabilityFlags flags;
    if(!s||!SCNetworkReachabilityGetFlags(target,&flags))return;
    pthread_mutex_lock(&s->lock);
    Callback *c=s->callback;
    Boolean deliver=c&&(!s->delivered||s->flags!=flags);
    if(deliver){s->flags=flags;s->delivered=TRUE;__atomic_add_fetch(&c->refs,1,__ATOMIC_RELAXED);}
    pthread_mutex_unlock(&s->lock);
    if(deliver){c->function(target,flags,c->context.info);callbackRelease(c);}
}
Boolean SCNetworkReachabilitySetCallback(SCNetworkReachabilityRef target,
        SCNetworkReachabilityCallBack callback,SCNetworkReachabilityContext *context) {
    ReachState *s=reachState(target);if(!s||(context&&context->version))return FALSE;
    Callback *next=NULL;
    if(callback){
        next=calloc(1,sizeof(*next));if(!next)return FALSE;
        next->refs=1;next->function=callback;if(context)next->context=*context;
        if(next->context.retain)next->context.info=(void *)next->context.retain(next->context.info);
    }
    pthread_mutex_lock(&s->lock);Callback *old=s->callback;
    s->callback=next;s->delivered=FALSE;
    pthread_mutex_unlock(&s->lock);callbackRelease(old);return TRUE;
}
Boolean SCNetworkReachabilityScheduleWithRunLoop(SCNetworkReachabilityRef target,
        CFRunLoopRef loop,CFStringRef mode) {
    ReachState *s=reachState(target);
    if(!s||!loop||!mode||CFGetTypeID(loop)!=CFRunLoopGetTypeID()||CFGetTypeID(mode)!=CFStringGetTypeID())return FALSE;
    pthread_mutex_lock(&s->lock);
    for(Registration *r=s->registrations;r;r=r->next)if(r->loop==loop&&CFEqual(r->mode,mode)){
        pthread_mutex_unlock(&s->lock);return TRUE;
    }
    Registration *r=calloc(1,sizeof(*r));
    if(!r){pthread_mutex_unlock(&s->lock);return FALSE;}
    r->source=SFSCNetworkSourceCreate(NULL,target,0,reachChanged,NULL);
    if(!r->source){free(r);pthread_mutex_unlock(&s->lock);return FALSE;}
    r->loop=(CFRunLoopRef)CFRetain(loop);r->mode=CFRetain(mode);
    r->next=s->registrations;s->registrations=r;
    CFRunLoopAddSource(loop,r->source,mode);CFRunLoopWakeUp(loop);
    pthread_mutex_unlock(&s->lock);return TRUE;
}
Boolean SCNetworkReachabilityUnscheduleFromRunLoop(SCNetworkReachabilityRef target,
        CFRunLoopRef loop,CFStringRef mode) {
    ReachState *s=reachState(target);if(!s||!loop||!mode)return FALSE;
    CFRetain(target);pthread_mutex_lock(&s->lock);Registration **slot=&s->registrations;
    while(*slot&&!((*slot)->loop==loop&&CFEqual((*slot)->mode,mode)))slot=&(*slot)->next;
    Registration *r=*slot;if(r)*slot=r->next;
    pthread_mutex_unlock(&s->lock);
    if(r){CFRunLoopRemoveSource(r->loop,r->source,r->mode);CFRelease(r->source);CFRelease(r->mode);CFRelease(r->loop);free(r);}
    CFRelease(target);return r!=NULL;
}
