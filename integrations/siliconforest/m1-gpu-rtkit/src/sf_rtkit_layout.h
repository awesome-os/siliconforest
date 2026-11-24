/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Native mailbox layout/IRQ validation. Hardware offsets follow Linux
 * drivers/mailbox/apple-mailbox.c and T8103 DT (Asahi Linux Contributors).
 * Resource conventions follow m1n1 (MIT, Asahi Linux Contributors).
 */
#ifndef SF_RTKIT_LAYOUT_H
#define SF_RTKIT_LAYOUT_H
#include "sf_rtkit_platform.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>
/* No MMIO/publication. Failure preserves the caller's resource record. */
static inline int sf_rtkit_mbox_layout(bool m3,uint64_t phys,uint64_t size,
                                      const uint32_t irq[4],struct sf_rtkit_mbox_res *out)
{
    uint64_t offset=m3?0:SF_RTKIT_ASC_MBOX_OFFSET;
    uint64_t span=m3?size:SF_RTKIT_ASC_MBOX_SIZE;
    if(!out || !irq || !phys || size<(m3?0xb0:offset+span) || phys>UINT64_MAX-size)return -EINVAL;
    uint32_t sorted[4];memcpy(sorted,irq,sizeof(sorted));
    for(unsigned i=1;i<4;++i)for(unsigned j=i;j && sorted[j-1]>sorted[j];--j) {
        uint32_t v=sorted[j];sorted[j]=sorted[j-1];sorted[j-1]=v;
    }
    if(sorted[0]>UINT32_MAX-3)return -EINVAL;
    for(unsigned i=1;i<4;++i)if(sorted[i]!=sorted[0]+i)return -EINVAL;
    struct sf_rtkit_mbox_res r=*out;
    r.asc_phys=phys;r.asc_size=size;r.mbox_phys=phys+offset;r.mbox_size=span;
    r.linux_compatible=m3?"apple,m3-mailbox-v2":"apple,asc-mailbox-v4";
    memcpy(r.aic_listed,irq,sizeof(r.aic_listed));
    r.aic_send_empty=sorted[0];r.aic_send_not_empty=sorted[1];
    r.aic_recv_empty=sorted[2];r.aic_recv_not_empty=sorted[3];
    *out=r;return 0;
}
#endif
