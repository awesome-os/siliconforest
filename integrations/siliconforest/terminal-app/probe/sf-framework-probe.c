// SPDX-License-Identifier: MIT
// Linked against a framework wrapper of ../compat at its install path, importing a name only
// the wrapper has: it loads only where that path is the wrapper (a link to /usr/lib's library
// lacks the name). The installer's proof for CoreFoundation.framework, Foundation.framework and
// IOKit.framework. Each imports a name of the newest additions (Activity Monitor's,
// ../../activity-monitor/libs), so a wrapper of before does not pass and is replaced. Since 14
// November (evening) NSIndexPath is in libFoundation, not in the Foundation wrapper: the Foundation
// probe passes only when the class comes from libFoundation and the wrapper defines none of its own (a wrapper
// of before has its own copy: every program then warns that the class is implemented twice).
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#ifdef PROBE_COREFOUNDATION
extern void *CFStringTokenizerCreate(void *alloc, void *string, long location, long length, unsigned long options, void *locale);
extern unsigned long CFArrayGetTypeID(void);                 // from the re-exported library
extern char sfNSCacheClass __asm__("_OBJC_CLASS_$_NSCache");  // CoreFoundation-am.m
int main(void) { printf("sf-corefoundation-framework-probe: the tokenizer is at %p, NSCache at %p, array type %lu\n", (void *)&CFStringTokenizerCreate, (void *)&sfNSCacheClass, CFArrayGetTypeID()); return CFArrayGetTypeID() != 0 ? 0 : 1; }
#endif
#ifdef PROBE_FOUNDATION
extern void *NSFileTypeForHFSTypeCode(uint32_t code);
extern void *NSStringFromClass(void *cls);                   // from the re-exported library
extern char sfNSIndexPathClass __asm__("_OBJC_CLASS_$_NSIndexPath");  // libFoundation, through the wrapper
int main(void) {
    void *s = NSFileTypeForHFSTypeCode(0x54455854);
    Dl_info i; const char *from = dladdr(&sfNSIndexPathClass, &i) && i.dli_fname ? i.dli_fname : "?";
    // the classes the wrapper image itself defines (libobjc's call, found at run time: the probe links no libobjc)
    const char **(*names)(const char *, unsigned *) = (const char **(*)(const char *, unsigned *))dlsym(RTLD_DEFAULT, "objc_copyClassNamesForImage");
    unsigned n = 0, own = 0;
    const char **list = names ? names("/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation", &n) : NULL;
    for (unsigned i = 0; list && i < n; i++)
        own += strcmp(list[i], "NSIndexPath") == 0;
    int ok = s != 0 && strstr(from, "libFoundation") != NULL && names != NULL && own == 0;
    printf("sf-foundation-framework-probe: NSFileTypeForHFSTypeCode %s, NSStringFromClass %p, NSIndexPath from %s, in the wrapper %u: %s\n", s ? "answers" : "nil", (void *)&NSStringFromClass, from, own, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
#endif
#ifdef PROBE_IOKIT
extern void *IOPMCopyAssertionActivityAggregate(void);       // IOKit-am.c
extern void *IOServiceMatching(const char *name);            // from the re-exported library
int main(void) { void *m = IOServiceMatching("IOPlatformExpertDevice"); printf("sf-iokit-framework-probe: IOPMCopyAssertionActivityAggregate %p, IOServiceMatching %s\n", (void *)&IOPMCopyAssertionActivityAggregate, m ? "answers" : "nil"); return m != 0 ? 0 : 1; }
#endif
