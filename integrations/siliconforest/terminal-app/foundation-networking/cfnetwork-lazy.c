/* SPDX-License-Identifier: MIT */
/* (TERM7) The four CFHTTPMessage functions Foundation's networking (Finch's NSURLSession, linked into libFoundation)
   uses to write a request's header, taken from CFNetwork when a request is first written. libFoundation does not
   link CFNetwork: CFNetwork re-exports libFoundation (a cycle) and loads SystemConfiguration, which every program
   that uses Foundation would then load. These definitions are hidden (not exported from libFoundation). Without
   CFNetwork a message cannot be made: CFHTTPMessageCreateEmpty answers NULL and the request fails. */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

typedef struct __CFHTTPMessage *CFHTTPMessageRef;
#define HIDDEN __attribute__((visibility("hidden")))

static CFHTTPMessageRef (*createEmpty)(CFAllocatorRef, Boolean);
static Boolean (*appendBytes)(CFHTTPMessageRef, const UInt8 *, CFIndex);
static CFDataRef (*copySerialized)(CFHTTPMessageRef);
static Boolean (*headerComplete)(CFHTTPMessageRef);
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void load(void) {
    void *h = dlopen("/System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork", RTLD_NOW | RTLD_GLOBAL);
    if (h == NULL) {
        fprintf(stderr, "Foundation: CFNetwork is not loadable (%s): HTTP requests cannot be written\n", dlerror());
        return;
    }
    appendBytes = dlsym(h, "CFHTTPMessageAppendBytes");
    copySerialized = dlsym(h, "CFHTTPMessageCopySerializedMessage");
    headerComplete = dlsym(h, "CFHTTPMessageIsHeaderComplete");
    if (appendBytes && copySerialized && headerComplete)
        createEmpty = dlsym(h, "CFHTTPMessageCreateEmpty");
}

HIDDEN CFHTTPMessageRef CFHTTPMessageCreateEmpty(CFAllocatorRef alloc, Boolean isRequest) {
    pthread_once(&once, load);
    return createEmpty ? createEmpty(alloc, isRequest) : NULL;
}

/* the others are only reached with a message CFHTTPMessageCreateEmpty made, so CFNetwork is loaded */
HIDDEN Boolean CFHTTPMessageAppendBytes(CFHTTPMessageRef m, const UInt8 *bytes, CFIndex n) {
    return appendBytes ? appendBytes(m, bytes, n) : false;
}

HIDDEN CFDataRef CFHTTPMessageCopySerializedMessage(CFHTTPMessageRef m) {
    return copySerialized ? copySerialized(m) : NULL;
}

HIDDEN Boolean CFHTTPMessageIsHeaderComplete(CFHTTPMessageRef m) {
    return headerComplete ? headerComplete(m) : false;
}
