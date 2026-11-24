// SPDX-License-Identifier: MIT
// sf-heap: where a program's heap grows. A sampling recorder of malloc's own allocation events
// (libmalloc's malloc_logger hook, which every malloc_zone_* call and malloc/calloc/realloc/free
// reports to) for systems without MallocStackLogging, leaks(1) or heap(1).
//
//   DYLD_INSERT_LIBRARIES=/path/libsfheap.dylib program
//   touch /tmp/sfheap.mark      a new epoch starts: the report counts what was allocated after it
//   touch /tmp/sfheap.dump      the report is appended to /tmp/sfheap-<pid>.txt (SFHEAP_OUT=dir)
//                               (each program with the library acts when the file's time changes)
//   SFHEAP_RATE=bytes           one allocation is recorded per RATE bytes allocated (default 4096);
//                               each recorded one stands for RATE bytes (or its size if larger)
//   SFHEAP_PROGRAM=text         only the program whose path contains TEXT records
//
// The report: malloc's zone statistics (bytes in use), then the recorded allocations still alive
// that were made after the last mark, by allocation stack (estimated bytes, count, the Objective-C
// class or CoreFoundation type of an example block), then the same by class. Comparing two reports
// a few minutes apart shows what keeps growing; a stack whose blocks are freed later drops out.
// Stacks are walked through the frame-pointer chain; names are what dladdr finds (image+offset too,
// for llvm-symbolizer against the build).
#include <dlfcn.h>
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <objc/runtime.h>

typedef void(malloc_logger_t)(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t result,
                              uint32_t skip);
extern malloc_logger_t *malloc_logger;

#define T_ALLOC 2u
#define T_FREE 4u
#define DEPTH 24
#define LIVE_BITS 19
#define LIVE_CAP (1u << LIVE_BITS)
#define STACK_BITS 17
#define STACK_CAP (1u << STACK_BITS)

typedef struct { uintptr_t ptr; uint32_t size, stack, epoch, weight; } Live;
typedef struct { uint64_t hash; uint32_t depth; uintptr_t pc[DEPTH]; } Stack;

static Live *live;              // open addressing, linear probing, backward-shift deletion
static uint32_t liveCount;
static Stack *stacks;           // stacks[0] unused
static uint32_t *stackIndex;    // hash -> stacks index
static uint32_t stackCount = 1;
static os_unfair_lock tableLock = OS_UNFAIR_LOCK_INIT;
static _Atomic long budget;
static long rate = 4096;
static _Atomic uint32_t epoch;
static _Atomic unsigned long dropped;

static void *mapZero(size_t bytes) {
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static inline uint32_t slotOf(uintptr_t p) { return (uint32_t)((p >> 4) * 0x9E3779B97F4A7C15ull >> (64 - LIVE_BITS)); }

static int walk(uintptr_t *out) {
    uintptr_t *fp = __builtin_frame_address(0);
    pthread_t self = pthread_self();
    uintptr_t top = (uintptr_t)pthread_get_stackaddr_np(self), bottom = top - pthread_get_stacksize_np(self);
    int n = 0;
    while (n < DEPTH && (uintptr_t)fp > bottom && (uintptr_t)fp < top && ((uintptr_t)fp & 7) == 0) {
        uintptr_t lr = fp[1] & 0x0000007fffffffffull;
        if (lr == 0) break;
        out[n++] = lr;
        uintptr_t *next = (uintptr_t *)fp[0];
        if (next <= fp) break;
        fp = next;
    }
    return n;
}

static uint32_t internStack(uintptr_t *pc, int depth) {  // tableLock held
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < depth; i++) h = (h ^ pc[i]) * 1099511628211ull;
    uint32_t s = (uint32_t)(h >> (64 - STACK_BITS));
    for (uint32_t probe = 0; probe < STACK_CAP; probe++, s = (s + 1) & (STACK_CAP - 1)) {
        uint32_t idx = stackIndex[s];
        if (idx == 0) {
            if (stackCount >= STACK_CAP / 2) return 0;
            idx = stackCount++;
            stacks[idx].hash = h; stacks[idx].depth = (uint32_t)depth;
            memcpy(stacks[idx].pc, pc, sizeof(uintptr_t) * (size_t)depth);
            stackIndex[s] = idx;
            return idx;
        }
        if (stacks[idx].hash == h && stacks[idx].depth == (uint32_t)depth) return idx;
    }
    return 0;
}

static void liveInsert(uintptr_t p, uint32_t size, uint32_t stack, uint32_t weight) {  // tableLock held
    if (liveCount >= LIVE_CAP * 3 / 4) { atomic_fetch_add(&dropped, 1); return; }
    uint32_t s = slotOf(p);
    while (live[s].ptr != 0 && live[s].ptr != p) s = (s + 1) & (LIVE_CAP - 1);
    if (live[s].ptr == 0) liveCount++;
    live[s] = (Live){p, size, stack, atomic_load(&epoch), weight};
}

static void liveRemove(uintptr_t p) {  // tableLock held
    uint32_t s = slotOf(p);
    while (live[s].ptr != p) {
        if (live[s].ptr == 0) return;
        s = (s + 1) & (LIVE_CAP - 1);
    }
    live[s].ptr = 0; liveCount--;
    uint32_t hole = s;
    for (uint32_t j = (s + 1) & (LIVE_CAP - 1); live[j].ptr != 0; j = (j + 1) & (LIVE_CAP - 1)) {
        uint32_t home = slotOf(live[j].ptr);
        // move j into the hole unless its home lies cyclically in (hole, j]
        if (hole <= j ? (home <= hole || home > j) : (home <= hole && home > j)) {
            live[hole] = live[j]; live[j].ptr = 0; hole = j;
        }
    }
}

static void hook(uint32_t type, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t result, uint32_t skip) {
    (void)a1; (void)skip;
    uintptr_t freed = 0, made = 0; size_t size = 0;
    if ((type & (T_ALLOC | T_FREE)) == (T_ALLOC | T_FREE)) { freed = a2; made = result; size = a3; }  // realloc
    else if (type & T_ALLOC) { made = result; size = a2; }
    else if (type & T_FREE) { freed = a2; }
    if (freed == made) freed = 0;  // realloc in place: keep the record (its size is the old one)
    uint32_t stack = 0, weight = 0;
    if (made != 0) {
        long left = atomic_fetch_sub_explicit(&budget, (long)size, memory_order_relaxed) - (long)size;
        if (left <= 0) {
            atomic_store_explicit(&budget, rate, memory_order_relaxed);
            weight = (uint32_t)((long)size > rate ? (long)size : rate);
            uintptr_t pc[DEPTH]; int depth = walk(pc);
            os_unfair_lock_lock(&tableLock);
            stack = internStack(pc, depth);
            liveInsert(made, (uint32_t)size, stack, weight);
            if (freed) liveRemove(freed);
            os_unfair_lock_unlock(&tableLock);
            return;
        }
    }
    if (freed != 0 && liveCount != 0) {
        os_unfair_lock_lock(&tableLock);
        liveRemove(freed);
        os_unfair_lock_unlock(&tableLock);
    }
}

/* ---- the report (on its own thread; it allocates, so it never holds tableLock while doing so) ---- */

static Class *classSet; static uint32_t classCap;
static int isClass(uintptr_t w) {
    if (w == 0 || classCap == 0) return 0;
    uint32_t s = (uint32_t)((w >> 3) * 0x9E3779B97F4A7C15ull >> 32) & (classCap - 1);
    while (classSet[s] != NULL) { if ((uintptr_t)classSet[s] == w) return 1; s = (s + 1) & (classCap - 1); }
    return 0;
}
static void buildClassSet(void) {
    unsigned n = 0; Class *all = objc_copyClassList(&n);
    uint32_t cap = 1; while (cap < n * 4u) cap <<= 1;
    if (classSet) munmap(classSet, sizeof(Class) * classCap);
    classSet = mapZero(sizeof(Class) * cap); classCap = classSet ? cap : 0;
    for (unsigned i = 0; i < n && classCap; i++) {
        uintptr_t w = (uintptr_t)all[i];
        uint32_t s = (uint32_t)((w >> 3) * 0x9E3779B97F4A7C15ull >> 32) & (classCap - 1);
        while (classSet[s] != NULL) s = (s + 1) & (classCap - 1);
        classSet[s] = all[i];
    }
    free(all);
}

typedef const char *(*TypeDesc)(uintptr_t);
static void kindOf(uintptr_t p, uint32_t size, char *buf, size_t len) {
    snprintf(buf, len, "malloc %u", size);
    if (size < 8 || size > 65536 || malloc_size((void *)p) == 0) return;  // freed meanwhile: do not read
    uintptr_t w0 = ((uintptr_t *)p)[0];
    uintptr_t cands[3] = {w0, w0 & 0x0000000ffffffff8ull, w0 & 0x00007ffffffffff8ull};
    for (int i = 0; i < 3; i++) if (isClass(cands[i])) {
        const char *name = class_getName((Class)cands[i]);
        if (size >= 16 && (strcmp(name, "__NSCFType") == 0 || strncmp(name, "__NSCF", 6) == 0)) {
            uint64_t info = ((uint64_t *)p)[1];
            snprintf(buf, len, "%s (CF type %llu)", name, (unsigned long long)((info >> 8) & 0x3ff));
        } else snprintf(buf, len, "%s", name);
        return;
    }
    if (w0 == 0 && size >= 16) {  // a CoreFoundation object without an isa
        uint64_t info = ((uint64_t *)p)[1];
        if (((info >> 8) & 0x3ff) != 0 && ((info >> 8) & 0x3ff) < 200)
            snprintf(buf, len, "CF type %llu (no isa), %u", (unsigned long long)((info >> 8) & 0x3ff), size);
    }
}

static void frameName(uintptr_t pc, char *buf, size_t len) {
    Dl_info info;
    if (dladdr((void *)(pc - 1), &info)) {
        const char *img = info.dli_fname ? strrchr(info.dli_fname, '/') : NULL;
        img = img ? img + 1 : (info.dli_fname ? info.dli_fname : "?");
        if (info.dli_sname)
            snprintf(buf, len, "%s+%lu  [%s+0x%lx]", info.dli_sname, (unsigned long)(pc - (uintptr_t)info.dli_saddr), img,
                     (unsigned long)(pc - (uintptr_t)info.dli_fbase));
        else snprintf(buf, len, "[%s+0x%lx]", img, (unsigned long)(pc - (uintptr_t)info.dli_fbase));
    } else snprintf(buf, len, "0x%lx", (unsigned long)pc);
}

typedef struct { uint32_t stack; uint64_t bytes; uint32_t count; uintptr_t example; uint32_t exSize; } Agg;
typedef struct { char name[96]; uint64_t bytes; uint32_t count; } ClassAgg;
static int byBytes(const void *a, const void *b) {
    uint64_t x = ((const Agg *)a)->bytes, y = ((const Agg *)b)->bytes; return x < y ? 1 : x > y ? -1 : 0;
}
static int byClassBytes(const void *a, const void *b) {
    uint64_t x = ((const ClassAgg *)a)->bytes, y = ((const ClassAgg *)b)->bytes; return x < y ? 1 : x > y ? -1 : 0;
}

static uint32_t markEpoch;
static void report(void) {
    buildClassSet();
    os_unfair_lock_lock(&tableLock);
    uint32_t n = 0; Live *snap = mapZero(sizeof(Live) * (liveCount + 1));
    if (snap) for (uint32_t i = 0; i < LIVE_CAP; i++) if (live[i].ptr != 0 && live[i].epoch >= markEpoch) snap[n++] = live[i];
    uint32_t total = liveCount, nstacks = stackCount;
    os_unfair_lock_unlock(&tableLock);
    if (!snap) return;
    Agg *agg = mapZero(sizeof(Agg) * nstacks);
    ClassAgg *cls = mapZero(sizeof(ClassAgg) * 4096); uint32_t ncls = 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        Agg *a = &agg[snap[i].stack];
        a->stack = snap[i].stack; a->bytes += snap[i].weight; a->count++; sum += snap[i].weight;
        if (!a->example) { a->example = snap[i].ptr; a->exSize = snap[i].size; }
        char k[96]; kindOf(snap[i].ptr, snap[i].size, k, sizeof k);
        if (strncmp(k, "malloc ", 7) == 0) {  // plain blocks by size class
            unsigned sz = snap[i].size, b = 16; while (b < sz) b <<= 1;
            snprintf(k, sizeof k, "malloc <=%u", b);
        }
        uint32_t j = 0; while (j < ncls && strcmp(cls[j].name, k) != 0) j++;
        if (j == ncls && ncls < 4096) { snprintf(cls[ncls].name, sizeof cls[ncls].name, "%s", k); ncls++; }
        if (j < 4096) { cls[j].bytes += snap[i].weight; cls[j].count++; }
    }
    qsort(agg, nstacks, sizeof(Agg), byBytes);
    qsort(cls, ncls, sizeof(ClassAgg), byClassBytes);
    char path[256]; const char *dir = getenv("SFHEAP_OUT");
    snprintf(path, sizeof path, "%s/sfheap-%d.txt", dir ? dir : "/tmp", getpid());
    FILE *f = fopen(path, "a");
    if (f) {
        malloc_statistics_t st, all = {0};
        vm_address_t *zones = NULL; unsigned nz = 0;
        malloc_get_all_zones(mach_task_self(), NULL, &zones, &nz);
        fprintf(f, "==== sf-heap pid %d epoch %u (mark %u), rate %ld\n", getpid(), atomic_load(&epoch), markEpoch, rate);
        for (unsigned z = 0; z < nz; z++) {
            malloc_zone_t *zone = (malloc_zone_t *)zones[z];
            memset(&st, 0, sizeof st); malloc_zone_statistics(zone, &st);
            fprintf(f, "zone %s: in use %zu bytes, %u blocks, allocated %zu\n", zone->zone_name ? zone->zone_name : "?",
                    st.size_in_use, st.blocks_in_use, st.size_allocated);
            all.size_in_use += st.size_in_use; all.blocks_in_use += st.blocks_in_use;
        }
        fprintf(f, "all zones: in use %zu bytes, %u blocks\n", all.size_in_use, all.blocks_in_use);
        fprintf(f, "recorded alive: %u (all epochs), %u since the mark = ~%llu bytes; stacks %u; dropped %lu\n", total, n,
                (unsigned long long)sum, nstacks, atomic_load(&dropped));
        fprintf(f, "-- by kind (since the mark)\n");
        for (uint32_t j = 0; j < ncls && j < 60; j++) fprintf(f, "%10llu %6u  %s\n", (unsigned long long)cls[j].bytes, cls[j].count, cls[j].name);
        fprintf(f, "-- by stack (since the mark)\n");
        for (uint32_t i = 0; i < nstacks && i < 40 && agg[i].bytes; i++) {
            char k[96]; kindOf(agg[i].example, agg[i].exSize, k, sizeof k);
            fprintf(f, "## %llu bytes, %u recorded, e.g. %s\n", (unsigned long long)agg[i].bytes, agg[i].count, k);
            Stack *s = &stacks[agg[i].stack];
            for (uint32_t d = 0; d < s->depth; d++) {
                char nm[256]; frameName(s->pc[d], nm, sizeof nm);
                if (strstr(nm, "libsfheap") || strstr(nm, "malloc_zone_") || strstr(nm, "_malloc_zone")) continue;
                fprintf(f, "   %s\n", nm);
            }
        }
        fclose(f);
    }
    munmap(snap, sizeof(Live) * (total + 1)); munmap(agg, sizeof(Agg) * nstacks); munmap(cls, sizeof(ClassAgg) * 4096);
}

static int touched(const char *path, long *seen) {  // the file's change time moved on (several programs watch it)
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    long t = (long)st.st_mtimespec.tv_sec * 1000 + st.st_mtimespec.tv_nsec / 1000000;
    if (*seen == -1) { *seen = t; return 0; }  // what was there before the program started does not count
    if (t == *seen) return 0;
    *seen = t; return 1;
}

static void *watcher(void *arg) {
    (void)arg;
    long markSeen = -1, dumpSeen = -1;
    touched("/tmp/sfheap.mark", &markSeen); touched("/tmp/sfheap.dump", &dumpSeen);
    if (markSeen == -1) markSeen = 0;
    if (dumpSeen == -1) dumpSeen = 0;
    for (;;) {
        sleep(1);
        if (touched("/tmp/sfheap.mark", &markSeen)) markEpoch = atomic_fetch_add(&epoch, 1) + 1;
        if (touched("/tmp/sfheap.dump", &dumpSeen)) report();
    }
    return NULL;
}

__attribute__((constructor)) static void sfheapStart(void) {
    const char *only = getenv("SFHEAP_PROGRAM");
    if (only) {
        char path[1024]; uint32_t len = sizeof path;
        extern int _NSGetExecutablePath(char *, uint32_t *);
        if (_NSGetExecutablePath(path, &len) != 0 || strstr(path, only) == NULL) return;
    }
    const char *r = getenv("SFHEAP_RATE"); if (r && atol(r) > 0) rate = atol(r);
    budget = rate;
    live = mapZero(sizeof(Live) * LIVE_CAP);
    stacks = mapZero(sizeof(Stack) * STACK_CAP / 2);
    stackIndex = mapZero(sizeof(uint32_t) * STACK_CAP);
    if (!live || !stacks || !stackIndex) return;
    pthread_t t; pthread_create(&t, NULL, watcher, NULL); pthread_detach(t);
    malloc_logger = hook;
}
