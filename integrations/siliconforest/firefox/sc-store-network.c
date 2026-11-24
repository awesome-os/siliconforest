/* SPDX-License-Identifier: MIT */
/* Read-only network notifications when configd is absent. This does not
 * implement or pretend to implement the general dynamic configuration store. */
#include "SCDynamicStoreInternal.h"
#include "sc-route-source.h"
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
Boolean SFSCStoreAvailable(void) {
    int fd=socket(PF_ROUTE,SOCK_RAW,0);if(fd<0)return FALSE;close(fd);return TRUE;
}
static Boolean validArray(CFArrayRef array,Boolean pattern) {
    if(!array)return TRUE;if(CFGetTypeID(array)!=CFArrayGetTypeID())return FALSE;
    for(CFIndex i=0;i<CFArrayGetCount(array);i++){
        CFStringRef key=CFArrayGetValueAtIndex(array,i);char bytes[4096];
        if(CFGetTypeID(key)!=CFStringGetTypeID()||
          !(CFStringHasPrefix(key,CFSTR("State:/Network/"))||CFStringHasPrefix(key,CFSTR("Setup:/Network/")))||
          !CFStringGetCString(key,bytes,sizeof(bytes),kCFStringEncodingUTF8))return FALSE;
        if(pattern){regex_t regex;if(regcomp(&regex,bytes,REG_EXTENDED|REG_NOSUB))return FALSE;regfree(&regex);}
    }return TRUE;
}
Boolean SFSCStoreSetKeys(SCDynamicStorePrivateRef store,CFArrayRef keys,CFArrayRef patterns) {
    if(!validArray(keys,FALSE)||!validArray(patterns,TRUE)){_SCErrorSet(kSCStatusInvalidArgument);return FALSE;}
    CFMutableArrayRef k=keys?CFArrayCreateMutableCopy(NULL,0,keys):NULL;
    CFMutableArrayRef p=patterns?CFArrayCreateMutableCopy(NULL,0,patterns):NULL;
    if((keys&&!k)||(patterns&&!p)){if(k)CFRelease(k);if(p)CFRelease(p);_SCErrorSet(kSCStatusFailed);return FALSE;}
    pthread_mutex_lock(&lock);CFMutableArrayRef oldK=store->keys,oldP=store->patterns;
    store->keys=k;store->patterns=p;pthread_mutex_unlock(&lock);
    if(oldK)CFRelease(oldK);if(oldP)CFRelease(oldP);_SCErrorSet(kSCStatusOK);return TRUE;
}
static Boolean matches(CFStringRef key,CFArrayRef keys,CFArrayRef patterns) {
    if(keys&&CFArrayContainsValue(keys,CFRangeMake(0,CFArrayGetCount(keys)),key))return TRUE;
    char value[4096];if(!CFStringGetCString(key,value,sizeof(value),kCFStringEncodingUTF8))return FALSE;
    for(CFIndex i=0;patterns&&i<CFArrayGetCount(patterns);i++){
        char bytes[4096];regex_t regex;
        if(!CFStringGetCString(CFArrayGetValueAtIndex(patterns,i),bytes,sizeof(bytes),kCFStringEncodingUTF8))continue;
        if(regcomp(&regex,bytes,REG_EXTENDED|REG_NOSUB))continue;
        int yes=regexec(&regex,value,0,NULL,0)==0;regfree(&regex);if(yes)return TRUE;
    }return FALSE;
}
static void addKey(CFMutableArrayRef out,CFStringRef key,CFArrayRef keys,CFArrayRef patterns) {
    if(matches(key,keys,patterns)&&!CFArrayContainsValue(out,CFRangeMake(0,CFArrayGetCount(out)),key))CFArrayAppendValue(out,key);
}
static void storeChanged(CFTypeRef target,unsigned changes) {
    SCDynamicStorePrivateRef store=(SCDynamicStorePrivateRef)target;
    pthread_mutex_lock(&lock);
    CFArrayRef keys=store->keys?CFRetain(store->keys):NULL,patterns=store->patterns?CFRetain(store->patterns):NULL;
    pthread_mutex_unlock(&lock);
    CFMutableArrayRef out=CFArrayCreateMutable(NULL,0,&kCFTypeArrayCallBacks);
    /* A route event invalidates the interface network snapshot, not an EAPOL
     * state which this backend has never provided. Include removed interfaces. */
    CFMutableArrayRef names=CFArrayCreateMutable(NULL,0,&kCFTypeArrayCallBacks);
    struct if_nameindex *interfaces=if_nameindex();
    for(struct if_nameindex *i=interfaces;i&&i->if_index;i++){
        CFStringRef name=CFStringCreateWithCString(NULL,i->if_name,kCFStringEncodingUTF8);
        if(name){CFArrayAppendValue(names,name);CFRelease(name);}
    }if(interfaces)if_freenameindex(interfaces);
    pthread_mutex_lock(&lock);CFArrayRef previous=store->sfNetworkInterfaces;
    store->sfNetworkInterfaces=CFArrayCreateCopy(NULL,names);pthread_mutex_unlock(&lock);
    if(previous){for(CFIndex i=0;i<CFArrayGetCount(previous);i++){
        CFStringRef name=CFArrayGetValueAtIndex(previous,i);
        if(!CFArrayContainsValue(names,CFRangeMake(0,CFArrayGetCount(names)),name))CFArrayAppendValue(names,name);
    }CFRelease(previous);}
    const char *entities[]={"IPv4","IPv6","DNS"};
    for(CFIndex i=0;i<CFArrayGetCount(names);i++)for(unsigned e=0;e<3;e++){
        if(!(changes&SF_SC_ROUTE_CHANGED)&&e!=2)continue;
        CFStringRef key=CFStringCreateWithFormat(NULL,NULL,CFSTR("State:/Network/Service/%@/%s"),CFArrayGetValueAtIndex(names,i),entities[e]);
        addKey(out,key,keys,patterns);CFRelease(key);
        key=CFStringCreateWithFormat(NULL,NULL,CFSTR("State:/Network/Interface/%@/%s"),CFArrayGetValueAtIndex(names,i),entities[e]);
        addKey(out,key,keys,patterns);CFRelease(key);
    }
    addKey(out,CFSTR("State:/Network/Global/DNS"),keys,patterns);
    if(CFArrayGetCount(out)&&store->rlsFunction)store->rlsFunction((SCDynamicStoreRef)store,out,store->rlsContext.info);
    CFRelease(names);CFRelease(out);if(keys)CFRelease(keys);if(patterns)CFRelease(patterns);
}
CFRunLoopSourceRef SFSCStoreSource(CFAllocatorRef allocator,SCDynamicStorePrivateRef store,CFIndex order) {
    return SFSCNetworkSourceCreate(allocator,store,order,storeChanged,NULL);
}
