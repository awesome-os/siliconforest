#ifndef HOST_ABI_MMAN_H
#define HOST_ABI_MMAN_H

#include <stddef.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANON 0x1000
#define MAP_FAILED ((void *) -1)

void *mmap(void *, size_t, int, int, int, long);
int mprotect(void *, size_t, int);
int munmap(void *, size_t);

#endif
