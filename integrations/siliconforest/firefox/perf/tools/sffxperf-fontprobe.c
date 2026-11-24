/* SPDX-License-Identifier: MIT
 * FXPERF diagnostic (own Firefox instance only, DYLD_INSERT_LIBRARIES): who copies or maps whole font files.
 * Logs every read from the start of a font file >= 4 MiB (whole-file copies) and every file mmap >= 4 MiB of a
 * font, with the caller chain (frame pointers, dladdr names). Lines start with FXFONT. No behaviour change. */
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long counter;

static void chain(char *out, size_t size) {
    uintptr_t *fp = __builtin_frame_address(0);
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < 14 && fp && used + 8 < size; i++) {
        uintptr_t lr = fp[1];
        Dl_info info;
        if (lr == 0) break;
        if (i > 0) {
            const char *name = (dladdr((void *)lr, &info) && info.dli_sname) ? info.dli_sname : "?";
            const char *lib = info.dli_fname ? strrchr(info.dli_fname, '/') : NULL;
            used += snprintf(out + used, size - used, " <- %s(%s+0x%lx)", name, lib ? lib + 1 : "?",
                             info.dli_fbase ? (unsigned long)(lr - (uintptr_t)info.dli_fbase) : 0UL);
        }
        uintptr_t *next = (uintptr_t *)fp[0];
        if (next <= fp) break;
        fp = next;
    }
}

static ssize_t my_read(int fd, void *buf, size_t len) {
    off_t at = len >= (1u << 20) ? lseek(fd, 0, SEEK_CUR) : -1;  /* only large reads are looked at */
    ssize_t r = read(fd, buf, len);
    if (at == 0 && r > 0) {
        struct stat st;
        char path[MAXPATHLEN];
        if (fstat(fd, &st) == 0 && st.st_size >= (4 << 20) && fcntl(fd, F_GETPATH, path) == 0 && strstr(path, "/Fonts/")) {
            char stack[2048];
            chain(stack, sizeof stack);
            pthread_mutex_lock(&lock);
            fprintf(stderr, "FXFONT n=%lu read-from-start path=%s size=%lld first=%zd%s\n", ++counter, path, (long long)st.st_size, r, stack);
            pthread_mutex_unlock(&lock);
        }
    }
    return r;
}

static void *my_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    void *r = mmap(addr, len, prot, flags, fd, off);
    if (fd >= 0 && len >= (4u << 20)) {
        char path[MAXPATHLEN];
        if (fcntl(fd, F_GETPATH, path) == 0 && strstr(path, "/Fonts/")) {
            char stack[2048];
            chain(stack, sizeof stack);
            pthread_mutex_lock(&lock);
            fprintf(stderr, "FXFONT n=%lu mmap path=%s len=%zu at=%p%s\n", ++counter, path, len, r, stack);
            pthread_mutex_unlock(&lock);
        }
    }
    return r;
}

__attribute__((used)) static struct { const void *replacement, *original; } interposers[]
    __attribute__((section("__DATA,__interpose"))) = {
        {(const void *)my_read, (const void *)read},
        {(const void *)my_mmap, (const void *)mmap},
};
