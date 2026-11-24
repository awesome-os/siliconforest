/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SF_DART_RETIRE_H
#define SF_DART_RETIRE_H
#include "sf_dart_table.h"
#include <stdbool.h>
#include <errno.h>
/* Caller owns the stream lock and separately establishes producer/in-flight
 * DMA quiescence. A successful return proves only table/TLB retirement.
 * Clearing CPU page tables does not establish
 * that hardware stopped reaching backing pages. A failed invalidate poisons
 * this stream until reboot; no later caller may infer reclamation from the
 * cleared PTEs. Output is unchanged on failure. */
static inline int sf_dart_retire(struct sf_dart_tables *tables,bool *poisoned,
                                 uint64_t iova,uint64_t bytes,void *ctx,
                                 int (*invalidate)(void *),uint64_t *unmapped)
{
    if(!tables || !poisoned || !invalidate || !unmapped || !bytes ||
       (iova&(SF_DART_PAGE-1)) || (bytes&(SF_DART_PAGE-1)) ||
       iova>=(1ULL<<32) || bytes>(1ULL<<32)-iova)return -EINVAL;
    if(*poisoned)return -EIO;
    // An unreachable table is not an empty range. Validate the walk before
    // clearing anything, so callers cannot mistake a skipped leaf for safe
    // reclamation even when a later hardware flush would acknowledge.
    for(uint64_t at=0;at<bytes;at+=SF_DART_PAGE) {
        struct sf_dart_walk walk;
        int ret=sf_dart_table_walk(tables,iova+at,&walk);
        if(ret){*poisoned=true;return ret;}
    }
    uint64_t done=sf_dart_table_unmap(tables,iova,bytes);
    int ret=invalidate(ctx);
    if(ret){*poisoned=true;return ret;}
    *unmapped=done;return 0;
}
#endif
