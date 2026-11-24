// SPDX-License-Identifier: MIT
// sf-sample: /usr/bin/sample for SiliconForest. Activity Monitor's Sample button runs
//   /usr/bin/sample <pid> <seconds> -file /dev/stdout
// and shows (and parses into its call tree) what comes back. This one stops the target for each
// sample (task_suspend), reads every thread's registers (thread_get_state), walks each thread's
// frame-pointer chain in the target's memory (mach_vm_read_overwrite), and names the addresses from
// the symbol tables of the images the target has loaded (its dyld image list, the files on disk).
// The report is in the layout macOS's sample writes: a header, "Call graph:" (per thread, the
// frames from the outermost down with how many samples were in each), "Sort by top of stack" and
// "Binary Images".
//
//   sample <pid | name> [<seconds> [<interval ms>]] [-file <path>] [-mayDie] [-wait] [-fullPaths] [-e]
//
// Limits: frames are found through the frame pointers (a function without one, or one that has
// not set it up yet, hides its caller); no DWARF, no inlined frames; names are the nearest symbol at
// or below the address in the image's own symbol table (a stripped image names nothing but itself).
#include <errno.h>
#include <libproc.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#define MAXDEPTH 256
#define ADDR_MASK 0x0000007fffffffffULL   // a user address without pointer-authentication bits

// a process's name (proc_name is not in every libSystem: the BSD info carries it)
static int procName(pid_t pid, char *buf, size_t n) {
    struct proc_bsdinfo b;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &b, sizeof(b)) != (int)sizeof(b)) return 0;
    snprintf(buf, n, "%s", b.pbi_name[0] ? b.pbi_name : b.pbi_comm);
    return (int)strlen(buf);
}

// ---- images and symbols ----
typedef struct { uint64_t addr; const char *name; } Sym;
typedef struct {
    uint64_t load, end;          // where it is in the target
    char path[1024];
    const char *leaf;
    uint8_t uuid[16];
    int haveUUID, loaded;
    Sym *syms; size_t nsyms;
} Image;

static Image *images; static size_t nimages;

static int bySymAddr(const void *a, const void *b) {
    uint64_t x = ((const Sym *)a)->addr, y = ((const Sym *)b)->addr;
    return x < y ? -1 : x > y;
}

// the image's own file: its segments' extent, UUID and symbols, slid to where the target has it
static void loadImage(Image *im) {
    int fd;
    struct stat st;
    uint8_t *base;
    im->loaded = 1;
    if ((fd = open(im->path, O_RDONLY)) < 0) return;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(struct mach_header_64)) { close(fd); return; }
    base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) return;
    uint8_t *mh = base;
    size_t size = (size_t)st.st_size;
    // a universal file: the arm64 slice
    if (*(uint32_t *)base == 0xbebafeca) {
        uint32_t n = __builtin_bswap32(((uint32_t *)base)[1]);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t *fa = (uint32_t *)(base + 8 + i * 20);
            if (__builtin_bswap32(fa[0]) == CPU_TYPE_ARM64) { mh = base + __builtin_bswap32(fa[2]); size = __builtin_bswap32(fa[3]); break; }
        }
    }
    struct mach_header_64 *h = (struct mach_header_64 *)mh;
    if (h->magic != MH_MAGIC_64) { munmap(base, (size_t)st.st_size); return; }
    uint8_t *lc = mh + sizeof(*h);
    uint64_t textVM = 0, lo = UINT64_MAX, hi = 0;
    struct symtab_command *symtab = NULL;
    for (uint32_t i = 0; i < h->ncmds && lc + 8 <= mh + size; i++, lc += ((struct load_command *)lc)->cmdsize) {
        struct load_command *c = (struct load_command *)lc;
        if (c->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)c;
            if (strcmp(seg->segname, "__PAGEZERO") == 0) continue;
            if (strcmp(seg->segname, "__TEXT") == 0) textVM = seg->vmaddr;
            if (seg->vmaddr < lo) lo = seg->vmaddr;
            if (seg->vmaddr + seg->vmsize > hi) hi = seg->vmaddr + seg->vmsize;
        } else if (c->cmd == LC_UUID) {
            memcpy(im->uuid, ((struct uuid_command *)c)->uuid, 16); im->haveUUID = 1;
        } else if (c->cmd == LC_SYMTAB) symtab = (struct symtab_command *)c;
    }
    uint64_t slide = im->load - textVM;
    if (hi > lo) im->end = hi + slide;
    if (symtab != NULL && symtab->symoff + (uint64_t)symtab->nsyms * sizeof(struct nlist_64) <= size && symtab->stroff < size) {
        struct nlist_64 *nl = (struct nlist_64 *)(mh + symtab->symoff);
        const char *strs = (const char *)(mh + symtab->stroff);
        im->syms = calloc(symtab->nsyms, sizeof(Sym));
        for (uint32_t i = 0; im->syms != NULL && i < symtab->nsyms; i++) {
            if ((nl[i].n_type & N_STAB) || (nl[i].n_type & N_TYPE) != N_SECT || nl[i].n_un.n_strx >= symtab->strsize) continue;
            const char *name = strs + nl[i].n_un.n_strx;
            if (name[0] == 0 || (name[0] == 'l' && name[1] == 't' && name[2] == 'm' && name[3] == 'p')) continue;
            im->syms[im->nsyms].addr = nl[i].n_value + slide;
            im->syms[im->nsyms].name = name[0] == '_' ? name + 1 : name;
            im->nsyms++;
        }
        qsort(im->syms, im->nsyms, sizeof(Sym), bySymAddr);
    }
    // the mapping stays: the names point into it
}

static Image *imageFor(uint64_t a) {
    Image *best = NULL;
    for (size_t i = 0; i < nimages; i++) {
        Image *im = &images[i];
        if (a >= im->load && (best == NULL || im->load > best->load)) {
            if (!im->loaded) loadImage(im);
            if (im->end == 0 || a < im->end) best = im;
        }
    }
    return best;
}

// "name  (in image) + offset", the way sample names a frame
static void nameAddress(uint64_t a, int returnAddress, char *out, size_t n) {
    uint64_t look = returnAddress && a > 0 ? a - 1 : a;
    Image *im = imageFor(look);
    if (im == NULL) { snprintf(out, n, "???"); return; }
    const Sym *s = NULL;
    size_t lo = 0, hi = im->nsyms;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (im->syms[mid].addr <= look) { s = &im->syms[mid]; lo = mid + 1; } else hi = mid; }
    if (s != NULL) snprintf(out, n, "%s  (in %s) + %llu", s->name, im->leaf, (unsigned long long)(a - s->addr));
    else snprintf(out, n, "???  (in %s) + %llu", im->leaf, (unsigned long long)(a - im->load));
}

// ---- the call tree ----
typedef struct Node { uint64_t addr; unsigned count; struct Node *child, *next; } Node;
typedef struct { uint64_t tid; char name[64]; Node root; } Thread;
static Thread *threads; static size_t nthreads, capThreads;

static Thread *threadFor(uint64_t tid) {
    for (size_t i = 0; i < nthreads; i++) if (threads[i].tid == tid) return &threads[i];
    if (nthreads == capThreads) { capThreads = capThreads ? capThreads * 2 : 16; threads = realloc(threads, capThreads * sizeof(Thread)); }
    memset(&threads[nthreads], 0, sizeof(Thread)); threads[nthreads].tid = tid;
    return &threads[nthreads++];
}

static Node *childFor(Node *parent, uint64_t addr) {
    Node **at = &parent->child;
    for (; *at != NULL; at = &(*at)->next) if ((*at)->addr == addr) return *at;
    *at = calloc(1, sizeof(Node)); (*at)->addr = addr;
    return *at;
}

// the leaf functions' counts (Sort by top of stack)
typedef struct { uint64_t addr; unsigned count; } Top;
static Top *tops; static size_t ntops, capTops;
static void countTop(uint64_t a) {
    for (size_t i = 0; i < ntops; i++) if (tops[i].addr == a) { tops[i].count++; return; }
    if (ntops == capTops) { capTops = capTops ? capTops * 2 : 256; tops = realloc(tops, capTops * sizeof(Top)); }
    tops[ntops].addr = a; tops[ntops++].count = 1;
}

static int readTarget(task_t task, uint64_t addr, void *buf, size_t n) {
    mach_vm_size_t got = 0;
    return mach_vm_read_overwrite(task, addr, n, (mach_vm_address_t)(uintptr_t)buf, &got) == KERN_SUCCESS && got == n;
}

static unsigned samples;

static void sampleOnce(task_t task) {
    thread_act_array_t list; mach_msg_type_number_t n = 0;
    if (task_suspend(task) != KERN_SUCCESS) return;
    if (task_threads(task, &list, &n) == KERN_SUCCESS) {
        for (mach_msg_type_number_t i = 0; i < n; i++) {
            thread_identifier_info_data_t ident; mach_msg_type_number_t ic = THREAD_IDENTIFIER_INFO_COUNT;
            uint64_t tid = list[i];
            if (thread_info(list[i], THREAD_IDENTIFIER_INFO, (thread_info_t)&ident, &ic) == KERN_SUCCESS) tid = ident.thread_id;
            Thread *t = threadFor(tid);
            if (t->name[0] == 0) {
                thread_extended_info_data_t ext; mach_msg_type_number_t ec = THREAD_EXTENDED_INFO_COUNT;
                if (thread_info(list[i], THREAD_EXTENDED_INFO, (thread_info_t)&ext, &ec) == KERN_SUCCESS && ext.pth_name[0])
                    snprintf(t->name, sizeof(t->name), "%s", ext.pth_name);
            }
            arm_thread_state64_t st; mach_msg_type_number_t sc = ARM_THREAD_STATE64_COUNT;
            uint64_t frames[MAXDEPTH]; int depth = 0;
            if (thread_get_state(list[i], ARM_THREAD_STATE64, (thread_state_t)&st, &sc) == KERN_SUCCESS) {
                uint64_t pc = (uint64_t)__darwin_arm_thread_state64_get_pc(st) & ADDR_MASK;
                uint64_t fp = (uint64_t)__darwin_arm_thread_state64_get_fp(st) & ADDR_MASK;
                frames[depth++] = pc;
                while (depth < MAXDEPTH && fp != 0 && (fp & 7) == 0) {
                    uint64_t pair[2];
                    if (!readTarget(task, fp, pair, sizeof(pair))) break;
                    uint64_t next = pair[0] & ADDR_MASK, ret = pair[1] & ADDR_MASK;
                    if (ret == 0) break;
                    frames[depth++] = ret;
                    if (next <= fp) break;
                    fp = next;
                }
            }
            t->root.count++;
            Node *at = &t->root;
            for (int d = depth - 1; d >= 0; d--) { at = childFor(at, frames[d]); at->count++; }
            if (depth > 0) countTop(frames[0]);
            mach_port_deallocate(mach_task_self(), list[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)list, n * sizeof(thread_act_t));
    }
    task_resume(task);
    samples++;
}

// ---- the report ----
static FILE *out;
static int byCount(const void *a, const void *b) { return (int)(*(Node *const *)b)->count - (int)(*(Node *const *)a)->count; }

static void printNode(Node *node, int depth, char *marks) {
    // children by count, most first
    size_t n = 0; for (Node *c = node->child; c; c = c->next) n++;
    Node **kids = malloc((n ? n : 1) * sizeof(Node *)); size_t k = 0;
    for (Node *c = node->child; c; c = c->next) kids[k++] = c;
    qsort(kids, n, sizeof(Node *), byCount);
    for (size_t i = 0; i < n; i++) {
        char name[1200];
        // a column has its mark while the node at that depth has siblings still to come (the first column always)
        marks[depth] = (depth == 0 || i + 1 < n) ? "+!:|"[depth % 4] : ' ';
        fprintf(out, "    ");
        for (int d = 0; d <= depth; d++) fprintf(out, "%c ", marks[d]);
        nameAddress(kids[i]->addr, depth > 0 || kids[i]->child != NULL, name, sizeof(name));
        fprintf(out, "%u %s  [0x%llx]\n", kids[i]->count, name, (unsigned long long)kids[i]->addr);
        if (depth + 1 < MAXDEPTH) printNode(kids[i], depth + 1, marks);
    }
    marks[depth] = ' ';
    free(kids);
}

static int byTop(const void *a, const void *b) { return (int)((const Top *)b)->count - (int)((const Top *)a)->count; }

static volatile sig_atomic_t stop;
static void onSignal(int sig) { (void)sig; stop = 1; }

int main(int argc, char **argv) {
    const char *target = NULL, *file = NULL;
    double seconds = 10, intervalMs = 1;
    int positional = 0, mayDie = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-file") == 0 && i + 1 < argc) file = argv[++i];
        else if (strcmp(argv[i], "-mayDie") == 0) mayDie = 1;
        else if (argv[i][0] == '-' && argv[i][1] != 0) continue;   // -wait, -fullPaths, -e, ...: accepted
        else if (positional == 0) { target = argv[i]; positional++; }
        else if (positional == 1) { seconds = atof(argv[i]); positional++; }
        else if (positional == 2) { intervalMs = atof(argv[i]); positional++; }
    }
    if (target == NULL) {
        fprintf(stderr, "Usage: sample <pid | partial-executable-name> [<duration in seconds> [<sampling interval in milliseconds>]] [-file <filename>] [-mayDie]\n");
        return 1;
    }
    pid_t pid = 0;
    char *end;
    long v = strtol(target, &end, 10);
    if (*end == 0 && v > 0) pid = (pid_t)v;
    else {
        int count = proc_listallpids(NULL, 0);
        pid_t *pids = calloc((size_t)count + 16, sizeof(pid_t));
        count = proc_listallpids(pids, (count + 16) * (int)sizeof(pid_t));
        for (int i = 0; i < count && pid == 0; i++) {
            char nm[256] = "";
            if (procName(pids[i], nm, sizeof(nm)) > 0 && strstr(nm, target) != NULL && pids[i] != getpid()) pid = pids[i];
        }
    }
    if (pid <= 0) { fprintf(stderr, "sample: no process matches \"%s\"\n", target); return 1; }
    if (seconds <= 0) seconds = 10;
    if (intervalMs < 0.1) intervalMs = 1;

    task_t task;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "sample cannot examine process %d: task_for_pid failed (%s); sample runs as root here\n", pid, mach_error_string(kr));
        return 1;
    }
    char pname[256] = "?", ppath[PROC_PIDPATHINFO_MAXSIZE] = "";
    procName(pid, pname, sizeof(pname));
    proc_pidpath(pid, ppath, sizeof(ppath));
    struct proc_bsdinfo bsd; memset(&bsd, 0, sizeof(bsd));
    int haveBSD = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd, sizeof(bsd)) == (int)sizeof(bsd);
    char parent[256] = "?";
    if (haveBSD) procName((pid_t)bsd.pbi_ppid, parent, sizeof(parent));

    // the target's images, from dyld's list in its memory
    struct task_dyld_info dinfo; mach_msg_type_number_t dc = TASK_DYLD_INFO_COUNT;
    if (task_info(task, TASK_DYLD_INFO, (task_info_t)&dinfo, &dc) == KERN_SUCCESS && dinfo.all_image_info_addr != 0) {
        struct dyld_all_image_infos all;
        if (readTarget(task, dinfo.all_image_info_addr, &all, sizeof(all)) && all.infoArrayCount > 0 && all.infoArrayCount < 100000) {
            struct dyld_image_info *infos = calloc(all.infoArrayCount, sizeof(*infos));
            if (readTarget(task, (uint64_t)(uintptr_t)all.infoArray, infos, all.infoArrayCount * sizeof(*infos))) {
                images = calloc(all.infoArrayCount + 1, sizeof(Image));
                for (uint32_t i = 0; i < all.infoArrayCount; i++) {
                    Image *im = &images[nimages];
                    im->load = (uint64_t)(uintptr_t)infos[i].imageLoadAddress;
                    for (size_t k = 0; k + 1 < sizeof(im->path); k += 64) {
                        if (!readTarget(task, (uint64_t)(uintptr_t)infos[i].imageFilePath + k, im->path + k, 64)) break;
                        if (memchr(im->path + k, 0, 64)) break;
                    }
                    im->path[sizeof(im->path) - 1] = 0;
                    if (im->path[0] == 0) continue;
                    im->leaf = strrchr(im->path, '/') ? strrchr(im->path, '/') + 1 : im->path;
                    nimages++;
                }
                if (all.dyldImageLoadAddress != NULL) {
                    Image *im = &images[nimages++];
                    im->load = (uint64_t)(uintptr_t)all.dyldImageLoadAddress;
                    snprintf(im->path, sizeof(im->path), "%s", all.version >= 15 && all.dyldPath ? "/usr/lib/dyld" : "/usr/lib/dyld");
                    im->leaf = "dyld";
                }
            }
            free(infos);
        }
    }

    out = stdout;
    if (file != NULL && strcmp(file, "-") != 0 && strcmp(file, "/dev/stdout") != 0) {
        out = fopen(file, "w");
        if (out == NULL) { fprintf(stderr, "sample: cannot write %s: %s\n", file, strerror(errno)); return 1; }
    }
    signal(SIGINT, onSignal); signal(SIGTERM, onSignal);
    printf("Sampling process %d for %g seconds with %g millisecond%s of run time between samples\n", pid, seconds, intervalMs, intervalMs == 1 ? "" : "s");
    fflush(stdout);
    struct timespec t0, now; clock_gettime(CLOCK_MONOTONIC, &t0);
    time_t started = time(NULL);
    int died = 0;
    for (;;) {
        if (kill(pid, 0) != 0) { died = 1; break; }
        sampleOnce(task);
        clock_gettime(CLOCK_MONOTONIC, &now);
        double el = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
        if (el >= seconds || stop) break;
        usleep((useconds_t)(intervalMs * 1000));
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
    printf("Sampling completed, processing symbols...\n");
    if (died && !mayDie) fprintf(stderr, "sample: process %d ended while it was sampled\n", pid);
    if (out != stdout) printf("Sample analysis of process %d written to file %s\n", pid, file);
    fflush(stdout);

    char date[64]; struct tm tm; localtime_r(&started, &tm); strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S %z", &tm);
    char os[128] = "", kern[128] = ""; size_t len = sizeof(os);
    sysctlbyname("kern.ostype", os, &len, NULL, 0); len = sizeof(kern); sysctlbyname("kern.osrelease", kern, &len, NULL, 0);
    fprintf(out, "Analysis of sampling %s (pid %d) every %g millisecond%s\n", pname, pid, intervalMs, intervalMs == 1 ? "" : "s");
    fprintf(out, "Process:         %s [%d]\n", pname, pid);
    fprintf(out, "Path:            %s\n", ppath);
    if (nimages > 0) fprintf(out, "Load Address:    0x%llx\n", (unsigned long long)images[0].load);
    fprintf(out, "Code Type:       ARM64\n");
    fprintf(out, "Platform:        macOS\n");
    fprintf(out, "Parent Process:  %s [%d]\n", parent, haveBSD ? (int)bsd.pbi_ppid : 0);
    fprintf(out, "\nDate/Time:       %s\n", date);
    fprintf(out, "OS Version:      SiliconForest (%s %s)\n", os, kern);
    fprintf(out, "Report Version:  7\n");
    fprintf(out, "Analysis Tool:   /usr/bin/sample (SiliconForest sf-sample: frame pointers, the images' own symbol tables)\n");
    fprintf(out, "----\n\n");
    fprintf(out, "Call graph:\n");
    char marks[MAXDEPTH + 1];
    memset(marks, ' ', sizeof(marks));
    for (size_t i = 0; i < nthreads; i++) {
        Thread *t = &threads[i];
        fprintf(out, "    %u Thread_%llu%s%s%s\n", t->root.count, (unsigned long long)t->tid,
                i == 0 ? "   DispatchQueue_1: com.apple.main-thread  (serial)" : "", t->name[0] ? "   " : "", t->name);
        printNode(&t->root, 0, marks);
    }
    fprintf(out, "\nTotal number in stack (recursive counted multiple times):\n");
    fprintf(out, "\nSort by top of stack, same collapsed (when >= 5):\n");
    qsort(tops, ntops, sizeof(Top), byTop);
    for (size_t i = 0; i < ntops && tops[i].count >= 5; i++) {
        char name[1200], *plus;
        nameAddress(tops[i].addr, 0, name, sizeof(name));
        if ((plus = strstr(name, ") + ")) != NULL) plus[1] = 0;
        fprintf(out, "        %-60s %u\n", name, tops[i].count);
    }
    fprintf(out, "\nBinary Images:\n");
    for (size_t i = 0; i < nimages; i++) {
        Image *im = &images[i];
        if (!im->loaded) loadImage(im);
        char uuid[40] = "";
        if (im->haveUUID) {
            const uint8_t *u = im->uuid;
            snprintf(uuid, sizeof(uuid), "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                     u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        }
        fprintf(out, "%18llx - %18llx  %s <%s> %s\n", (unsigned long long)im->load, (unsigned long long)(im->end ? im->end - 1 : im->load),
                im->leaf, uuid, im->path);
    }
    fprintf(out, "\nSample analysis of process %d: %u samples in %.2f s%s\n", pid, samples, elapsed, died ? " (the process ended)" : "");
    if (out != stdout) fclose(out);
    return 0;
}
