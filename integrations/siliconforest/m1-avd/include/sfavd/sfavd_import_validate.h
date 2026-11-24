/* SPDX-License-Identifier: MIT */
#ifndef SFAVD_IMPORT_VALIDATE_H
#define SFAVD_IMPORT_VALIDATE_H
#include "sfavd_ioctl.h"
#ifdef KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#define SFAVD_IMPORT_PAGE UINT64_C(16384)
#define SFAVD_IMPORT_MAX_BYTES (UINT64_C(128) << 20)
#define SFAVD_IMPORT_USER_LIMIT UINT64_C(0x0001000000000000)
/* Pure admission validation only. The kernel must additionally pin writable
 * current-task pages, verify physical non-aliasing, and commit transactionally. */
static inline int sfavd_import_validate(const struct sfavd_import *r,
                                       uint32_t count, uint64_t required) {
    if (!r || !count || count > SFAVD_MAX_BUFFERS || r->count != count ||
        r->flags || r->reserved || !required || required > SFAVD_IMPORT_MAX_BYTES)
        return EINVAL;
    required = (required + SFAVD_IMPORT_PAGE - 1) & ~(SFAVD_IMPORT_PAGE - 1);
    for (uint32_t i = 0; i < count; i++) {
        uint64_t a=r->buffers[i].address,n=r->buffers[i].size;
        if (!a || (a & (SFAVD_IMPORT_PAGE-1)) || (n & (SFAVD_IMPORT_PAGE-1)) ||
            n < required || n > SFAVD_IMPORT_MAX_BYTES ||
            a >= SFAVD_IMPORT_USER_LIMIT || n > SFAVD_IMPORT_USER_LIMIT-a)
            return EINVAL;
        for (uint32_t j = 0; j < i; j++) {
            uint64_t b=r->buffers[j].address,m=r->buffers[j].size;
            if (a < b+m && b < a+n) return EINVAL;
        }
    }
    for (uint32_t i=count;i<SFAVD_MAX_BUFFERS;i++)
        if (r->buffers[i].address || r->buffers[i].size) return EINVAL;
    return 0;
}
#endif
