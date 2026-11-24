// SPDX-License-Identifier: MIT
// sf-objc-probe: the guarded installer's proof of a new /usr/lib/libobjc.A.dylib
// (../build-libobjc.nix: SF_POOL_TRACE read once at start, ../patches/objc-pool-trace-read-once.patch).
// SiliconForest's libobjc asked getenv("SF_POOL_TRACE") at every autorelease, so a variable set while the
// program runs turned the trace on; the new one reads it once, with the runtime's other variables.
// Exits 0 when an autorelease after setenv("SF_POOL_TRACE") writes no trace line (and retain, release,
// autorelease and pool pop still balance), 1 when it does (the library of before), 2 when the variable
// was set from the start (nothing to tell apart).
#include <fcntl.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *);
extern id objc_autorelease(id);
extern id objc_retain(id);
extern void objc_release(id);
extern unsigned long _objc_rootRetainCount(id);

int main(void)
{
    if (getenv("SF_POOL_TRACE") != NULL) return 2;
    Class cls = objc_getClass("NSObject");
    id o = ((id (*)(Class, SEL))objc_msgSend)(cls, sel_registerName("new"));
    void *pool = objc_autoreleasePoolPush();
    objc_autorelease(objc_retain(o));
    objc_autoreleasePoolPop(pool);

    setenv("SF_POOL_TRACE", "1", 1);
    int fds[2];
    if (pipe(fds) != 0) return 3;
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fflush(stderr);
    int saved = dup(2);
    dup2(fds[1], 2);
    pool = objc_autoreleasePoolPush();
    objc_autorelease(objc_retain(o));
    objc_autoreleasePoolPop(pool);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    close(fds[1]);
    char buf[512];
    ssize_t n = read(fds[0], buf, sizeof buf - 1);
    buf[n > 0 ? n : 0] = 0;
    unsigned long rc = _objc_rootRetainCount(o);
    ((void (*)(id, SEL))objc_msgSend)(o, sel_registerName("release"));
    int traced = strstr(buf, "pool:") != NULL;
    printf("sf-objc-probe: SF_POOL_TRACE set while running: %s; retain count after the pools %lu\n",
           traced ? "traced (read at every autorelease)" : "not traced (read once at start)", rc);
    return traced || rc != 1 ? 1 : 0;
}
