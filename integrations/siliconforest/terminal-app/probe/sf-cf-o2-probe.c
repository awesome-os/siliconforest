// SPDX-License-Identifier: MIT
// sf-cf-o2-probe: the guarded installer's proof of an optimised /usr/lib/libCoreFoundation.dylib
// (../build-corefoundation.nix --arg autorelease false: -O2 alone, rehearsed before the functional
// change). Exits 0 when CFRetain's first instructions do not store its argument to the frame
// (stur x0, [x29, #-n]: how -O0 code begins, as SiliconForest's CoreFoundation was built), 1 when they
// do; and in both builds CoreFoundation's basic calls must answer (strings, a dictionary, a number).
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const uint32_t *code = (const uint32_t *)(uintptr_t)&CFRetain;
    int spill = -1;
    for (int i = 0; i < 12; i++)
        if ((code[i] & 0xFFE00FFFu) == 0xF80003A0u) { spill = i; break; }  // stur x0, [x29, #imm9]
    CFStringRef s = CFStringCreateWithFormat(NULL, NULL, CFSTR("%d-%s"), 7, "probe");
    int n = 42;
    CFNumberRef num = CFNumberCreate(NULL, kCFNumberIntType, &n);
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(d, s, num);
    char buf[32] = {0};
    int back = 0;
    CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
    CFNumberGetValue((CFNumberRef)CFDictionaryGetValue(d, s), kCFNumberIntType, &back);
    int works = strcmp(buf, "7-probe") == 0 && back == 42 && CFDictionaryGetCount(d) == 1;
    CFRelease(d); CFRelease(num); CFRelease(s);
    printf("CoreFoundation: CFRetain %s (first instructions), calls %s\n",
           spill < 0 ? "optimised" : "unoptimised (-O0)", works ? "answer" : "FAIL");
    return spill < 0 && works ? 0 : 1;
}
