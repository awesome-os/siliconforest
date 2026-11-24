/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SF_DART_GROUP_H
#define SF_DART_GROUP_H
#include "sf_dart.h"
#include <errno.h>
/* Mapping policy shared by the native LinuxKPI adapter and host register
 * models. Native functions retain their real table/TLB ownership contract. */
static inline int sf_dart_group_map_deferred(struct sf_dart_stream *a,struct sf_dart_stream *b,
                                            uint64_t iova,uint64_t phys,uint64_t bytes,unsigned prot)
{
    if(!a || a==b)return -EINVAL;
    int ret=sf_dart_map_deferred(a,iova,phys,bytes,prot);
    if(ret){(void)sf_dart_flush(a);return ret;}
    if(b) {
        ret=sf_dart_map_deferred(b,iova,phys,bytes,prot);
        if(ret) {
            uint64_t done;
            // Only undo a stream in which this call succeeded. The failed
            // peer may contain a foreign/firmware PTE that must stay intact.
            (void)sf_dart_unmap_checked(a,iova,bytes,&done);
            (void)sf_dart_flush(b); // also flush a failed map's internal rollback
            return ret;
        }
    }
    return 0;
}
static inline int sf_dart_group_flush(struct sf_dart_stream *a,struct sf_dart_stream *b)
{
    int first=sf_dart_flush(a);
    int second=b?sf_dart_flush(b):0;
    return first?first:second;
}
static inline int sf_dart_group_unmap_checked(struct sf_dart_stream *a,struct sf_dart_stream *b,
                                              uint64_t iova,uint64_t bytes,uint64_t *unmapped)
{
    if(!unmapped || a==b)return -EINVAL;
    uint64_t first=0,second=0;
    int ret=sf_dart_unmap_checked(a,iova,bytes,&first);if(ret)return ret;
    if(b) {
        ret=sf_dart_unmap_checked(b,iova,bytes,&second);if(ret)return ret;
        if(second<first)first=second;
    }
    *unmapped=first;return 0;
}
#endif
