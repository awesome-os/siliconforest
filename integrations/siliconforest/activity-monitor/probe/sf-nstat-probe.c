// SPDX-License-Identifier: MIT
// sf-nstat-probe [seconds]: the per-process network bytes Activity Monitor would show, through the installed
// NetworkStatistics (libs/NetworkStatistics.c) the way Activity Monitor drives it: a manager on a queue, every
// TCP and UDP source added, an update query each second; at the end one line per process with sources:
// pid, sources, rx/tx bytes and packets (the sum over its sources), and the raw message counts seen.
// SFNS_LIB=<path>: another build of the library.
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef CFTypeRef (*create_t)(CFAllocatorRef, dispatch_queue_t, void (^)(CFTypeRef, void *));
typedef int (*addall_t)(CFTypeRef);
typedef int (*query_t)(CFTypeRef, void (^)(void));
typedef CFDictionaryRef (*copy_t)(CFTypeRef);
typedef void (*descblock_t)(CFTypeRef, void (^)(CFDictionaryRef));

static CFTypeRef sources[4096];
static int nsources, ndesc, nquery;

static long long num(CFDictionaryRef d, const char *key) {
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFNumberRef n = d ? CFDictionaryGetValue(d, k) : NULL;
    long long v = -1;
    if (n) CFNumberGetValue(n, kCFNumberSInt64Type, &v);
    CFRelease(k);
    return v;
}

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 5;
    const char *path = getenv("SFNS_LIB") ? getenv("SFNS_LIB") : "/System/Library/PrivateFrameworks/NetworkStatistics.framework/Versions/A/NetworkStatistics";
    void *lib = dlopen(path, RTLD_NOW);
    if (!lib) { fprintf(stderr, "no NetworkStatistics: %s\n", dlerror()); return 1; }
    create_t create = dlsym(lib, "NStatManagerCreate");
    addall_t tcp = dlsym(lib, "NStatManagerAddAllTCP"), udp = dlsym(lib, "NStatManagerAddAllUDP");
    query_t query = dlsym(lib, "NStatManagerQueryAllSourcesUpdate");
    copy_t counts = dlsym(lib, "NStatSourceCopyCounts"), props = dlsym(lib, "NStatSourceCopyProperties");
    descblock_t setdesc = dlsym(lib, "NStatSourceSetDescriptionBlock");
    if (!create || !tcp || !udp || !query || !counts || !props || !setdesc) { fprintf(stderr, "symbols missing\n"); return 1; }
    dispatch_queue_t q = dispatch_queue_create("probe", NULL);
    CFTypeRef m = create(NULL, q, ^(CFTypeRef source, void *unused) {
        (void)unused;
        if (nsources < 4096) { CFRetain(source); sources[nsources++] = source; }
        setdesc(source, ^(CFDictionaryRef d) { (void)d; ndesc++; });
    });
    printf("added TCP %d UDP %d\n", tcp(m), udp(m));
    for (int i = 0; i < seconds; i++) {
        dispatch_semaphore_t s = dispatch_semaphore_create(0);
        query(m, ^{ nquery++; dispatch_semaphore_signal(s); });
        dispatch_semaphore_wait(s, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        sleep(1);
    }
    dispatch_sync(q, ^{
        printf("sources %d descriptions %d queries answered %d\n", nsources, ndesc, nquery);
        struct { long long pid, n, rxb, txb, rxp, txp; } per[512]; int np = 0;
        for (int i = 0; i < nsources; i++) {
            CFDictionaryRef p = props(sources[i]), c = counts(sources[i]);
            long long pid = num(p, "processID");
            int k;
            for (k = 0; k < np && per[k].pid != pid; k++) {}
            if (k == np && np < 512) { per[np].pid = pid; per[np].n = per[np].rxb = per[np].txb = per[np].rxp = per[np].txp = 0; np++; }
            if (k < 512) {
                per[k].n++;
                if (c) { per[k].rxb += num(c, "rxBytes"); per[k].txb += num(c, "txBytes"); per[k].rxp += num(c, "rxPackets"); per[k].txp += num(c, "txPackets"); }
            }
            if (p) CFRelease(p);
            if (c) CFRelease(c);
        }
        for (int k = 0; k < np; k++)
            printf("pid %lld sources %lld rx %lld B %lld pkts tx %lld B %lld pkts\n", per[k].pid, per[k].n, per[k].rxb, per[k].rxp, per[k].txb, per[k].txp);
    });
    return 0;
}
