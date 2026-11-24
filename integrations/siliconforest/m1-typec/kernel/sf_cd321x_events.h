/* SPDX-License-Identifier: GPL-2.0 */
/* EVENT1/CLEAR1 ordering from cd321x_interrupt(), Omacom linux-aurora
 * 6a08a08e drivers/usb/typec/tipd/core.c, Copyright Intel Corporation and
 * The Asahi Linux Contributors. Native caller serializes the whole operation
 * with port state sampling. This never changes the interrupt mask or enables
 * a GPIO/AIC interrupt; it can run on the existing slow sampler.
 */
#ifndef SF_CD321X_EVENTS_H
#define SF_CD321X_EVENTS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <sys/errno.h>
struct sf_cd321x_events_result {
    uint8_t pending[8];
    bool read_valid, acknowledge_attempted, acknowledged;
};
static inline int sf_cd321x_events_ack(void *ctx,
    int (*read)(void *,uint8_t,void *,size_t),
    int (*write)(void *,uint8_t,const void *,size_t),
    struct sf_cd321x_events_result *out)
{
    if(!ctx || !read || !write || !out)return EINVAL;
    memset(out,0,sizeof(*out));
    int ret=read(ctx,0x14,out->pending,sizeof(out->pending));
    if(ret)return ret;
    out->read_valid=true;
    uint8_t any=0;
    for(unsigned i=0;i<sizeof(out->pending);++i)any|=out->pending[i];
    if(!any)return 0;
    /* W1C only the observed events, including the upper bytes. Events that
     * arrive after this read and were not observed remain pending. Failed
     * writes are ambiguous, never counted as successful acknowledgements.
     */
    out->acknowledge_attempted=true;
    ret=write(ctx,0x18,out->pending,sizeof(out->pending));
    if(!ret)out->acknowledged=true;
    return ret;
}
#endif
