/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SF_DART_PAIR_H
#define SF_DART_PAIR_H
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
/* Find one run free in BOTH bitmaps. Caller holds the common allocator lock
 * and reserves the result in both before publishing it. No hardware fact. */
static inline int sf_dart_pair_find(const uint8_t *a,const uint8_t *b,
                                    uint64_t pages,uint64_t wanted,uint64_t *first)
{
    if(!a || !first || !wanted || wanted>pages)return -EINVAL;
    uint64_t run=0;
    for(uint64_t p=0;p<pages;++p) {
        unsigned mask=1u<<(p&7);
        run=((a[p>>3]&mask) || (b && (b[p>>3]&mask)))?0:run+1;
        if(run==wanted){*first=p+1-wanted;return 0;}
    }
    return -ENOSPC;
}
#endif
