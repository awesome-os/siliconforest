// SPDX-License-Identifier: MIT
// Probe for AppKit in a program without a window, as Homebrew's vim uses it: the program links
// AppKit by its macOS path (Versions/C), takes the general pasteboard, puts a string on it the
// way vim's os_macosx.m does (declareTypes:owner:, setString:forType:) and reads it back
// (availableTypeFromArray:, stringForType:), and asks NSSound for a sound that does not exist.
// Plain C over the Objective-C runtime, so it builds with the C cross toolchain.
// "SFAPPKIT ok" only if every check holds. No display is needed and none is contacted.
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef void *id;
typedef void *SEL;
extern id objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern void objc_msgSend(void);
extern id NSPasteboardTypeString;          // AppKit: binds at load, as in vim

#define SEND0(ret, obj, sel) ((ret (*)(id, SEL))objc_msgSend)((id)(obj), sel_registerName(sel))
#define SEND1(ret, obj, sel, a) ((ret (*)(id, SEL, id))objc_msgSend)((id)(obj), sel_registerName(sel), (id)(a))
#define SEND2(ret, obj, sel, a, b) ((ret (*)(id, SEL, id, id))objc_msgSend)((id)(obj), sel_registerName(sel), (id)(a), (id)(b))

static int bad;
static void check(const char *what, int ok, const char *detail)
{
    printf("SFAPPKIT %s %s %s\n", ok ? "ok  " : "FAIL", what, detail);
    if (!ok) bad++;
}

int main(void)
{
    char line[256], got[128] = "";
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    id pool = SEND0(id, SEND0(id, objc_getClass("NSAutoreleasePool"), "alloc"), "init");
    id pbClass = objc_getClass("NSPasteboard"), soundClass = objc_getClass("NSSound");
    snprintf(line, sizeof(line), "NSPasteboard=%p NSSound=%p NSPasteboardTypeString=%p", pbClass, soundClass, NSPasteboardTypeString);
    check("the classes vim binds are there", pbClass && soundClass && NSPasteboardTypeString, line);

    id pb = SEND0(id, pbClass, "generalPasteboard");
    check("+[NSPasteboard generalPasteboard]", pb != NULL, pb ? "an object" : "nil");

    CFStringRef text = CFSTR("put there by sf-appkit-probe");
    const void *types[1] = { NSPasteboardTypeString };
    CFArrayRef supported = CFArrayCreate(NULL, types, 1, &kCFTypeArrayCallBacks);
    long before = SEND0(long, pb, "changeCount");
    SEND2(long, pb, "declareTypes:owner:", supported, NULL);
    int set = SEND2(int, pb, "setString:forType:", text, NSPasteboardTypeString) & 0xff;
    long after = SEND0(long, pb, "changeCount");
    id best = SEND1(id, pb, "availableTypeFromArray:", supported);
    CFStringRef back = best ? SEND1(CFStringRef, pb, "stringForType:", NSPasteboardTypeString) : NULL;
    if (back) CFStringGetCString(back, got, sizeof(got), kCFStringEncodingUTF8);
    snprintf(line, sizeof(line), "set=%d changeCount %ld -> %ld type=%s read=[%s]", set, before, after, best ? "found" : "none", got);
    check("a string put on the pasteboard is read back", set && best && after > before && !strcmp(got, "put there by sf-appkit-probe"), line);

    id none = SEND1(id, soundClass, "soundNamed:", CFSTR("sf-no-such-sound"));
    check("+[NSSound soundNamed:] of a sound nobody has is nil", none == NULL, none ? "an object" : "nil");

    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("SFAPPKIT time in main: %ld ms\n", (long)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000));
    SEND0(void, pool, "drain");
    if (bad) {
        printf("SFAPPKIT failed=%d\n", bad);
        return 1;
    }
    printf("SFAPPKIT ok\n");
    return 0;
}
