/* SPDX-License-Identifier: MIT */
/* Linux ida_alloc_range includes its maximum. The frozen FreeBSD LinuxKPI
 * ida_simple_get excludes end, with end0 meaning through INT_MAX.
 * Reference: linux-aurora 6a08a08e lib/idr.c / include/linux/idr.h; native
 * fbsdkpi linux_idr.c / linux/idr.h. This helper allocates no identifiers. */
#ifndef SF_TB_IDA_RANGE_H
#define SF_TB_IDA_RANGE_H
#include <stdint.h>
#include <limits.h>
#include <errno.h>
static inline int sf_tb_ida_exclusive_end(uint32_t minimum,uint32_t maximum,
                                        uint32_t *end)
{
    if(!end)return -EINVAL;
    if(minimum>INT_MAX)return -ENOSPC;
    if(maximum>INT_MAX)maximum=INT_MAX;
    if(minimum>maximum)return -ENOSPC;
    *end=maximum==INT_MAX?0:maximum+1;
    return 0;
}
#endif
