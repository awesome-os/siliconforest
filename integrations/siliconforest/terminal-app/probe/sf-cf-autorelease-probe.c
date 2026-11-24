// SPDX-License-Identifier: MIT
// sf-cf-autorelease-probe: the guarded installer's proof of a new /usr/lib/libCoreFoundation.dylib
// (../build-corefoundation.nix). Exits 0 when CFAutorelease puts a CoreFoundation object in the
// autorelease pool (the pool's pop gives back the reference it took), 1 when it returns the object
// unreleased as SiliconForest's CoreFoundation did (a leak at every call).
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>

extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *pool);

int main(void) {
    CFMutableArrayRef array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    CFRetain(array);
    CFRetain(dict);
    void *pool = objc_autoreleasePoolPush();
    CFAutorelease(array);
    CFAutorelease(dict);
    objc_autoreleasePoolPop(pool);
    long a = (long)CFGetRetainCount(array), d = (long)CFGetRetainCount(dict);
    printf("CFAutorelease: retain counts after the pool: array %ld, dictionary %ld (1 = autoreleased)\n", a, d);
    int ok = a == 1 && d == 1;
    CFRelease(array);
    CFRelease(dict);
    if (ok) {  // and the object of a pool still in place stays alive until its pop
        CFStringRef s = CFStringCreateWithFormat(NULL, NULL, CFSTR("probe %d"), 42);
        pool = objc_autoreleasePoolPush();
        CFRetain(s);
        CFAutorelease(s);
        ok = CFGetRetainCount(s) == 2 || CFGetRetainCount(s) > 1000;  // a tagged or constant string
        objc_autoreleasePoolPop(pool);
        CFRelease(s);
    }
    return ok ? 0 : 1;
}
