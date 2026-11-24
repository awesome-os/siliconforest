// SPDX-License-Identifier: MIT
// sf-bench: micro-benchmarks of the core libraries every program uses (libSystem's malloc, locks,
// string functions, stdio, getenv, libdispatch; libobjc's message send, retain/release and
// autorelease pools). docs/roadmap/issues/SF-Y-terminal-app.md, "Performance: optimised core
// libraries". Run it with the libraries in use, or with DYLD_LIBRARY_PATH naming a test copy:
//   sf-bench [rounds]        one line per test: name, ns per operation (best and median of rounds)
// Plain C against libobjc's C interface (no Foundation), so that it measures libSystem and libobjc only.
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *);
extern id objc_autorelease(id);
extern id objc_retain(id);
extern void objc_release(id);

static mach_timebase_info_data_t tb;
static double ns(uint64_t t) { return (double)t * tb.numer / tb.denom; }
static volatile uintptr_t sink;

#define N 200000
typedef void (*bench_fn)(long n);

static void b_malloc16(long n) { for (long i = 0; i < n; i++) { void *p = malloc(16); sink = (uintptr_t)p; free(p); } }
static void b_malloc64(long n) { for (long i = 0; i < n; i++) { void *p = malloc(64); sink = (uintptr_t)p; free(p); } }
static void b_malloc256(long n) { for (long i = 0; i < n; i++) { void *p = malloc(256); sink = (uintptr_t)p; free(p); } }
static void b_malloc4k(long n) { for (long i = 0; i < n; i++) { void *p = malloc(4096); sink = (uintptr_t)p; free(p); } }
static void b_malloc64k(long n) { for (long i = 0; i < n / 10; i++) { void *p = malloc(65536); sink = (uintptr_t)p; free(p); } }
static void *batch[1000];
static void b_batch(long n) {   // 1000 live objects of mixed small sizes, then all freed
    for (long r = 0; r < n / 1000; r++) {
        for (int i = 0; i < 1000; i++) batch[i] = malloc(16 + (i * 37) % 480);
        for (int i = 0; i < 1000; i++) free(batch[(i * 7) % 1000]);
    }
}
static void b_calloc128(long n) { for (long i = 0; i < n; i++) { void *p = calloc(1, 128); sink = (uintptr_t)p; free(p); } }
static void b_realloc(long n) { for (long i = 0; i < n / 10; i++) { void *p = malloc(16); for (int s = 32; s <= 1024; s *= 2) { p = realloc(p, s); sink = (uintptr_t)p; } free(p); } }
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static void b_mutex(long n) { for (long i = 0; i < n; i++) { pthread_mutex_lock(&mu); sink++; pthread_mutex_unlock(&mu); } }
static os_unfair_lock ul = OS_UNFAIR_LOCK_INIT;
static void b_unfair(long n) { for (long i = 0; i < n; i++) { os_unfair_lock_lock(&ul); sink++; os_unfair_lock_unlock(&ul); } }
static char src[1 << 20], dst[1 << 20];
static void b_memcpy64(long n) { for (long i = 0; i < n; i++) { memcpy(dst + (i & 63), src, 64); sink = dst[5]; } }
static void b_memcpy4k(long n) { for (long i = 0; i < n / 4; i++) { memcpy(dst, src + (i & 7), 4096); sink = dst[5]; } }
static void b_memcpy1m(long n) { for (long i = 0; i < n / 2000; i++) { memcpy(dst, src, 1 << 20); sink = dst[5]; } }
static void b_memset4k(long n) { for (long i = 0; i < n / 4; i++) { memset(dst, (int)i, 4096); sink = dst[5]; } }
static void b_strlen(long n) { for (long i = 0; i < n; i++) sink += strlen(src + (i & 7)); }
static char fmt[128];
static void b_snprintf(long n) { for (long i = 0; i < n / 4; i++) sink += snprintf(fmt, sizeof fmt, "%ld %s %.3f", i, "row", (double)i / 7); }
static void b_getenv(long n) { for (long i = 0; i < n; i++) sink += (uintptr_t)getenv("SF_BENCH_ABSENT"); }
static dispatch_queue_t q;
static dispatch_semaphore_t sem;
static void ping(void *ctx) { (void)ctx; dispatch_semaphore_signal(sem); }
static void b_dispatch_rt(long n) { for (long i = 0; i < n / 20; i++) { dispatch_async_f(q, NULL, ping); dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER); } }
static void nop(void *ctx) { sink += (uintptr_t)ctx; }
static void b_dispatch_sync(long n) { for (long i = 0; i < n; i++) dispatch_sync_f(q, (void *)1, nop); }
static void b_dispatch_once(long n) { static dispatch_once_t once; for (long i = 0; i < n; i++) dispatch_once_f(&once, (void *)1, nop); }
static id obj; static SEL sel_self, sel_new, sel_release;
static Class nsobject;
static void b_msgsend(long n) { for (long i = 0; i < n; i++) sink += (uintptr_t)((id (*)(id, SEL))objc_msgSend)(obj, sel_self); }
static void b_retain(long n) { for (long i = 0; i < n; i++) { objc_retain(obj); objc_release(obj); } }
static id objs[10];
static void b_pool(long n) {     // a pool with ten different autoreleased objects, pushed and popped
    for (long i = 0; i < n / 10; i++) {
        void *p = objc_autoreleasePoolPush();
        for (int k = 0; k < 10; k++) objc_autorelease(objc_retain(objs[k]));
        objc_autoreleasePoolPop(p);
    }
}
static void b_newobj(long n) {   // +new and -release of an NSObject: malloc, objc, free
    for (long i = 0; i < n; i++) { id o = ((id (*)(Class, SEL))objc_msgSend)(nsobject, sel_new); ((void (*)(id, SEL))objc_msgSend)(o, sel_release); }
}

static struct { const char *name; bench_fn fn; long per; } T[] = {
    {"malloc_free_16", b_malloc16, 1}, {"malloc_free_64", b_malloc64, 1}, {"malloc_free_256", b_malloc256, 1},
    {"malloc_free_4k", b_malloc4k, 1}, {"malloc_free_64k", b_malloc64k, 10}, {"malloc_batch_mixed", b_batch, 2},
    {"calloc_free_128", b_calloc128, 1}, {"realloc_16_to_1k", b_realloc, 10},
    {"pthread_mutex", b_mutex, 1}, {"os_unfair_lock", b_unfair, 1},
    {"memcpy_64", b_memcpy64, 1}, {"memcpy_4k", b_memcpy4k, 4}, {"memcpy_1m", b_memcpy1m, 2000}, {"memset_4k", b_memset4k, 4},
    {"strlen_256", b_strlen, 1}, {"snprintf", b_snprintf, 4}, {"getenv_absent", b_getenv, 1},
    {"dispatch_async_roundtrip", b_dispatch_rt, 20}, {"dispatch_sync", b_dispatch_sync, 1}, {"dispatch_once", b_dispatch_once, 1},
    {"objc_msgSend", b_msgsend, 1}, {"retain_release", b_retain, 1}, {"pool_push_10_pop", b_pool, 10}, {"nsobject_new_release", b_newobj, 1},
};

static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

int main(int argc, char **argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 5;
    if (rounds < 1 || rounds > 50) rounds = 5;
    mach_timebase_info(&tb);
    memset(src, 'a', sizeof src - 1);
    src[256] = 0;   // strlen reads about 256 bytes
    q = dispatch_queue_create("sf-bench", NULL);
    sem = dispatch_semaphore_create(0);
    nsobject = objc_getClass("NSObject");
    sel_self = sel_registerName("self"); sel_new = sel_registerName("new"); sel_release = sel_registerName("release");
    obj = ((id (*)(Class, SEL))objc_msgSend)(nsobject, sel_new);
    for (int k = 0; k < 10; k++) objs[k] = ((id (*)(Class, SEL))objc_msgSend)(nsobject, sel_new);
    for (size_t t = 0; t < sizeof T / sizeof T[0]; t++) {
        if (T[t].fn == NULL) continue;
        double r[50];
        T[t].fn(N / 10);   // warm
        for (int i = 0; i < rounds; i++) {
            uint64_t t0 = mach_absolute_time();
            T[t].fn(N);
            r[i] = ns(mach_absolute_time() - t0) / ((double)N / T[t].per);
        }
        qsort(r, rounds, sizeof r[0], cmp);
        printf("%-26s %10.1f ns best %10.1f ns median\n", T[t].name, r[0], r[rounds / 2]);
        fflush(stdout);
    }
    return 0;
}
