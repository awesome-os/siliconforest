// SPDX-License-Identifier: MIT
// sf-trace: log the Objective-C methods of chosen classes as an application calls them, with
// the integer and floating-point argument registers on entry and the result registers on return.
// For finding where an application's path through a framework differs from what is expected.
//
//   DYLD_INSERT_LIBRARIES=/path/libsftrace.dylib SFTRACE='TTView,TTLogicalScreen,NSView:drawRect:' program
//     Class            every instance method the class itself defines
//     +Class           its class methods
//     Class:selector   one method
//   SFTRACE_SKIP='sel1,sel2'   selectors left alone (retain, release, autorelease, dealloc and
//                              .cxx_destruct always are)
//   SFTRACE_FILE=path          where the lines go (default: standard error)
//   SFTRACE_PROGRAM=text       trace only the program whose path contains this
//
//   SFTRACE_OBJECTS=1          name the objects among the first four arguments and in the result
//                              (class, and the text of a string, a number or a selector-free
//                              description is not asked for: nothing of the object is called
//                              except -UTF8String of a string)
//
// A line on entry:  "> -[Class sel] self x2 x3 x4 x5 | d0 d1 d2 d3 @ms", indented by depth;
// on return:        "< sel x0 x1 | d0 d1 d2 d3 @ms" (ms: milliseconds since the tracer loaded).
// The return address of a traced call is replaced while it runs: an exception thrown through a
// traced method does not unwind. A diagnostic tool, not for a program that is to keep running.
#include <objc/runtime.h>
#include <objc/message.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 4096
struct regs { uint64_t x[10]; uint64_t fp, lr; double d[8]; };
struct rets { uint64_t x0, x1, x8, pad; double d[4]; };
static struct { IMP imp; SEL sel; const char *cls; char sign; } table[MAXT];
static int count;
static FILE *out;
struct frame { uint64_t lr; int idx; };
static __thread struct frame stack[512];
static __thread int depth;

extern void sftrace_thunks(void);

#include <time.h>
// milliseconds since the tracer was loaded: the "@n" at the end of each line
static unsigned long long now_ms(void)
{
    static struct timespec start;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if (start.tv_sec == 0 && start.tv_nsec == 0) start = t;
    return (unsigned long long)((t.tv_sec - start.tv_sec) * 1000 + (t.tv_nsec - start.tv_nsec) / 1000000);
}

// ---- naming objects without trusting the pointer ------------------------------------------
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach/mach_vm.h>
static int objects;
static uintptr_t *classes; static unsigned nclasses;
static Class string_class;
static int cmp(const void *a, const void *b) { uintptr_t x = *(const uintptr_t *)a, y = *(const uintptr_t *)b; return x < y ? -1 : x > y; }
static void load_classes(void)
{
    unsigned n = 0; Class *list = objc_copyClassList(&n);
    classes = malloc((n + 1) * sizeof(uintptr_t));
    for (unsigned i = 0; i < n; i++) classes[i] = (uintptr_t)list[i];
    nclasses = n; free(list);
    qsort(classes, nclasses, sizeof(uintptr_t), cmp);
    string_class = objc_getClass("NSString");
}
// "" or " <Class>" or " \"text\"" for a value that is an object of a registered class
static const char *name_of(uint64_t v, char *buf, size_t size)
{
    buf[0] = 0;
    if (!objects || v < 0x100000000ull || (v & 7)) return buf;
    uint64_t isa = 0; mach_vm_size_t got = 0;
    if (mach_vm_read_overwrite(mach_task_self(), v, 8, (mach_vm_address_t)&isa, &got) != KERN_SUCCESS || got != 8) return buf;
    uintptr_t c = (uintptr_t)(isa & 0x0000000ffffffff8ull);
    if (bsearch(&c, classes, nclasses, sizeof(uintptr_t), cmp) == NULL) return buf;
    Class k = (Class)c;
    for (Class s = k; s; s = class_getSuperclass(s))
        if (s == string_class) {
            const char *t = ((const char *(*)(id, SEL))objc_msgSend)((id)v, sel_registerName("UTF8String"));
            snprintf(buf, size, " \"%.60s\"", t ? t : "");
            return buf;
        }
    snprintf(buf, size, " <%s>", class_getName(k));
    return buf;
}

IMP sftrace_enter(struct regs *r)
{
    int idx = (int)r->x[9];
    if (depth < 512) { stack[depth].lr = r->lr; stack[depth].idx = idx; }
    char b2[96], b3[96], b4[96], b5[96];
    fprintf(out, "%*s> %c[%s %s] %llx %llx%s %llx%s %llx%s %llx%s | %g %g %g %g @%llu\n", depth > 40 ? 40 : depth, "", table[idx].sign, table[idx].cls,
            sel_getName(table[idx].sel), (unsigned long long)r->x[0], (unsigned long long)r->x[2], name_of(r->x[2], b2, sizeof b2),
            (unsigned long long)r->x[3], name_of(r->x[3], b3, sizeof b3), (unsigned long long)r->x[4], name_of(r->x[4], b4, sizeof b4),
            (unsigned long long)r->x[5], name_of(r->x[5], b5, sizeof b5), r->d[0], r->d[1], r->d[2], r->d[3], now_ms());
    depth++;
    return table[idx].imp;
}

uint64_t sftrace_leave(struct rets *r)
{
    if (depth > 0) depth--;
    int idx = depth < 512 ? stack[depth].idx : 0;
    char b0[96];
    fprintf(out, "%*s< %s %llx%s %llx | %g %g %g %g @%llu\n", depth > 40 ? 40 : depth, "", sel_getName(table[idx].sel),
            (unsigned long long)r->x0, name_of(r->x0, b0, sizeof b0), (unsigned long long)r->x1, r->d[0], r->d[1], r->d[2], r->d[3], now_ms());
    return depth < 512 ? stack[depth].lr : 0;
}

static int skipped(const char *name, const char *skip)
{
    static const char *never[] = { "retain", "release", "autorelease", "dealloc", ".cxx_destruct", "retainCount", "class", "self", "isKindOfClass:", "respondsToSelector:", "methodSignatureForSelector:", "forwardInvocation:", NULL };
    for (int i = 0; never[i]; i++) if (!strcmp(name, never[i])) return 1;
    size_t n = strlen(name);
    for (const char *p = skip; p && *p; ) {
        const char *e = strchr(p, ','); size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == n && !strncmp(p, name, n)) return 1;
        p = e ? e + 1 : NULL;
    }
    return 0;
}

static void hook(Class c, Method m, const char *cls, char sign)
{
    if (count >= MAXT) return;
    table[count].imp = method_getImplementation(m);
    table[count].sel = method_getName(m);
    table[count].cls = cls; table[count].sign = sign;
    method_setImplementation(m, (IMP)((char *)sftrace_thunks + 8 * count));
    count++;
}

__attribute__((constructor)) static void sftrace_init(void)
{
    const char *spec = getenv("SFTRACE"), *skip = getenv("SFTRACE_SKIP"), *file = getenv("SFTRACE_FILE");
    out = stderr;
    if (spec == NULL) return;
    // SFTRACE_PROGRAM: only the program whose path contains this (the variables are inherited by
    // everything a script starts on the way to the program meant)
    const char *only = getenv("SFTRACE_PROGRAM");
    if (only != NULL) {
        char path[4096]; uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) != 0 || strstr(path, only) == NULL) return;
    }
    if (file) out = fopen(file, "a");
    if (out == NULL) out = stderr;
    setvbuf(out, NULL, _IOLBF, 0);
    objects = getenv("SFTRACE_OBJECTS") != NULL;
    if (objects) load_classes();
    char *copy = strdup(spec);
    for (char *item = strtok(copy, ","); item; item = strtok(NULL, ",")) {
        char sign = '-';
        if (*item == '+') { sign = '+'; item++; }
        char *one = strchr(item, ':');
        if (one) *one++ = 0;
        Class c = objc_getClass(item);
        if (c == Nil) continue;
        Class target = sign == '+' ? object_getClass((id)c) : c;
        if (one) {
            Method m = class_getInstanceMethod(target, sel_registerName(one));
            if (m == NULL) { fprintf(out, "sftrace: no %c[%s %s]\n", sign, item, one); continue; }
            // a method inherited from a superclass is hooked there: every subclass is traced
            hook(target, m, item, sign);
            continue;
        }
        unsigned n = 0;
        Method *list = class_copyMethodList(target, &n);
        for (unsigned i = 0; i < n; i++)
            if (!skipped(sel_getName(method_getName(list[i])), skip)) hook(target, list[i], item, sign);
        free(list);
    }
    if (count) {
        fprintf(out, "sftrace: %d methods\n", count);
        // the programs this one starts (a terminal's shell) are not traced
        unsetenv("DYLD_INSERT_LIBRARIES");
    }
}
