// SPDX-License-Identifier: MIT
// surface-lookup-race-probe: one thread drops the last reference to a surface while another thread
// looks the same surface up from its Mach port, as Firefox's parent does (IPC thread lookup of a tile
// surface while the main thread's CARenderer releases the previous one). An old IOSurface revives a
// finalizing object in its lookup table and then crashes reading its unmapped header (SIGSEGV/SIGBUS);
// a fixed one returns a live or a freshly mapped surface every time. CPU-backed surfaces only: no GPU.
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct __IOSurface *IOSurfaceRef;
extern const CFStringRef kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat;
IOSurfaceRef IOSurfaceCreate(CFDictionaryRef);
mach_port_t IOSurfaceCreateMachPort(IOSurfaceRef);
IOSurfaceRef IOSurfaceLookupFromMachPort(mach_port_t);
size_t IOSurfaceGetWidth(IOSurfaceRef);
uint32_t IOSurfaceGetID(IOSurfaceRef);

static _Atomic mach_port_t current = MACH_PORT_NULL;
static _Atomic long lookups, mismatches, done;

static void *looker(void *unused) {
    (void)unused;
    while (!atomic_load(&done)) {
        mach_port_t port = atomic_load(&current);
        if (port == MACH_PORT_NULL) continue;
        IOSurfaceRef s = IOSurfaceLookupFromMachPort(port);
        if (s) {
            // touches the header: an unmapped (finalized) object faults here
            if (IOSurfaceGetWidth(s) != 64) atomic_fetch_add(&mismatches, 1);
            CFRelease(s);
            atomic_fetch_add(&lookups, 1);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    long rounds = argc > 1 ? atol(argv[1]) : 200000;
    int width = 64, height = 64, bpe = 4, format = 0x42475241;
    CFMutableDictionaryRef props = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    CFStringRef keys[] = {kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat};
    int values[] = {width, height, bpe, format};
    for (int i = 0; i < 4; i++) {
        CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &values[i]);
        CFDictionarySetValue(props, keys[i], n);
        CFRelease(n);
    }
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) pthread_create(&threads[i], NULL, looker, NULL);
    for (long r = 0; r < rounds; r++) {
        IOSurfaceRef s = IOSurfaceCreate(props);
        if (!s) { fprintf(stderr, "create failed at %ld\n", r); return 2; }
        mach_port_t port = IOSurfaceCreateMachPort(s);
        long seen = atomic_load(&lookups);
        atomic_store(&current, port);
        // let the lookers find this surface (they then look it up again and again), then drop the
        // creator's reference while they keep looking it up
        for (int wait = 0; wait < 200000 && atomic_load(&lookups) < seen + 2; wait++) {}
        for (volatile int spin = 0; spin < (int)(r % 64) * 4; spin++) {}
        CFRelease(s);  // the creator's last reference, racing the lookers
        long after = atomic_load(&lookups);
        for (int wait = 0; wait < 200000 && atomic_load(&lookups) < after + 2; wait++) {}
        atomic_store(&current, MACH_PORT_NULL);
        mach_port_deallocate(mach_task_self(), port);
        if ((r + 1) % 50000 == 0) {
            printf("round %ld lookups %ld mismatches %ld\n", r + 1, atomic_load(&lookups), atomic_load(&mismatches));
            fflush(stdout);
        }
    }
    atomic_store(&done, 1);
    for (int i = 0; i < 2; i++) pthread_join(threads[i], NULL);
    printf("PASS rounds %ld lookups %ld mismatches %ld\n", rounds, atomic_load(&lookups), atomic_load(&mismatches));
    return atomic_load(&mismatches) ? 1 : 0;
}
