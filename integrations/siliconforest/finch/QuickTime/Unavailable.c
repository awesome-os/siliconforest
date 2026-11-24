/* SPDX-License-Identifier: MIT */
/* Unavailable private-service entry points. No authorization, now-playing bus,
 * or performance recording is fabricated. Calling layout comes from the
 * already-held QuickTime arm64e call sites, not an Apple SDK header. */
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#if SF_KIND==1
const CFStringRef kTCCServiceScreenCapture=CFSTR("kTCCServiceScreenCapture");
int32_t TCCAccessPreflight(CFStringRef service,CFDictionaryRef options){
 (void)service;(void)options;return 1; /* denied; no TCC service/prompt exists */
}
void TCCAccessRequest(CFStringRef service,CFDictionaryRef options,void (^completion)(Boolean)){
 (void)service;(void)options;
 if(completion)dispatch_async(dispatch_get_global_queue(0,0),^{completion(false);});
}
#elif SF_KIND==2
const void *MRMediaRemoteGetLocalOrigin(void){return NULL;}
void MRMediaRemoteSetNowPlayingTintColor(const void *origin,float r,float g,float b,float a){
 (void)origin;(void)r;(void)g;(void)b;(void)a;
 static dispatch_once_t once;dispatch_once(&once,^{fputs("MediaRemote: now-playing tint unavailable (no service)\n",stderr);});
}
#elif SF_KIND==3
uint64_t PAMarkOperationStart(const void *domain,const void *operation){
 (void)domain;(void)operation;return 0; /* no recording token */
}
void PAMarkOperationEnd(const void *domain,const void *operation){(void)domain;(void)operation;}
#endif
