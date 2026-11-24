// SPDX-License-Identifier: GPL-2.0
/* ACIO M3 RTKit startup and SRAM mapping from Linux drivers/thunderbolt/apple.c.
 * Copyright (c) Sven Peter <sven@kernel.org>
 * RTKit port: Copyright The Asahi Linux Contributors, GPL-2.0-only OR MIT.
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/soc/apple/rtkit.h>
#include <linux/soc/apple/mailbox.h>
#include "rtkit-internal.h"
#include "sf_rtkit_compat.h"
#include "sf_cio_rtkit.h"
#include "sf_apple_nhi.h"
struct sf_cio_rtkit {
    struct device *dev;
    struct apple_rtkit *rtk;
    void *sram;
    uint64_t phys;
    size_t span;
    struct { uint32_t count,errors,first_error; } ack[2];
};
void sf_cio_rtkit_observe_ack(void *engine,unsigned endpoint,int result)
{
    struct apple_rtkit *rtk=engine;
    if(!rtk || !rtk->cookie || (endpoint!=2 && endpoint!=4))return;
    struct sf_cio_rtkit *s=rtk->cookie;
    // Init can enable reception before returning its handle. Count only
    // the published real engine, whose lifetime the native owner retains.
    if(__atomic_load_n(&s->rtk,__ATOMIC_ACQUIRE)!=rtk)return;
    unsigned n=endpoint==2?0:1;
    __atomic_add_fetch(&s->ack[n].count,1,__ATOMIC_RELAXED);
    if(result){
        __atomic_add_fetch(&s->ack[n].errors,1,__ATOMIC_RELAXED);
        uint32_t none=0;
        __atomic_compare_exchange_n(&s->ack[n].first_error,&none,(uint32_t)result,
                                   false,__ATOMIC_RELAXED,__ATOMIC_RELAXED);
    }
}
int sf_cio_rtkit_snapshot(struct sf_cio_rtkit *s,struct sf_cio_rtkit_status *out)
{
    if(!s || !out)return -EINVAL;
    struct apple_rtkit *rtk=__atomic_load_n(&s->rtk,__ATOMIC_ACQUIRE);
    if(!rtk)return -ENODEV;
    struct apple_mbox *mbox=READ_ONCE(rtk->mbox);
    if(!mbox || !mbox->regs || READ_ONCE(mbox->removing) ||
       !READ_ONCE(mbox->active))return -ENODEV;
    struct sf_cio_rtkit_status v={0};
    v.protocol=READ_ONCE(rtk->version);
    v.ap_power=READ_ONCE(rtk->ap_power_state);v.iop_power=READ_ONCE(rtk->iop_power_state);
    v.running=apple_rtkit_is_running(rtk);v.crashed=READ_ONCE(rtk->crashed);
    v.shutting_down=READ_ONCE(rtk->shutting_down);
    size_t entries=READ_ONCE(rtk->syslog_n_entries),bytes=READ_ONCE(rtk->syslog_msg_size);
    size_t buffer=READ_ONCE(rtk->syslog_buffer.size);
    if(entries>255 || bytes>255 || buffer>s->span)return -EOVERFLOW;
    v.syslog_entries=entries;v.syslog_message_bytes=bytes;v.syslog_buffer_bytes=buffer;
    v.syslog_mapped=READ_ONCE(rtk->syslog_buffer.is_mapped) &&
        READ_ONCE(rtk->syslog_buffer.iomem)!=NULL;
    v.syslog_ack_count=__atomic_load_n(&s->ack[0].count,__ATOMIC_RELAXED);
    v.syslog_ack_errors=__atomic_load_n(&s->ack[0].errors,__ATOMIC_RELAXED);
    v.syslog_first_ack_error=__atomic_load_n(&s->ack[0].first_error,__ATOMIC_RELAXED);
    v.ioreport_ack_count=__atomic_load_n(&s->ack[1].count,__ATOMIC_RELAXED);
    v.ioreport_ack_errors=__atomic_load_n(&s->ack[1].errors,__ATOMIC_RELAXED);
    v.ioreport_first_ack_error=__atomic_load_n(&s->ack[1].first_error,__ATOMIC_RELAXED);
    v.mailbox_active=1;
    // ACIO1's verified iop,m3wrap-v2-acio binding uses this exact M3 layout
    // (pinned mailbox.c), not ASC offsets. These control reads do not pop a
    // FIFO. No SEND/RECV message, SRAM byte or IRQ ACK register is read.
    v.a2i_control=readl(mbox->regs+0x50);v.i2a_control=readl(mbox->regs+0x80);
    v.mailbox_irq_enable=readl(mbox->regs+0x48);
    if(v.a2i_control==UINT32_MAX || v.i2a_control==UINT32_MAX ||
       v.mailbox_irq_enable==UINT32_MAX)return -EIO;
    int error=sf_cio_mailbox_irq_snapshot(mbox->irq_recv_not_empty,
                                         mbox->irq_send_empty,v.mailbox_irq_state);
    if(error)return error;
    *out=v;return 0;
}
static int setup(void *cookie,struct apple_rtkit_shmem *bfr)
{
    struct sf_cio_rtkit *s=cookie;
    uint64_t off;
    int ret=sf_apple_cio_sram_offset(bfr->iova,bfr->size,s->phys,s->span,&off);
    if(ret)return ret;
    bfr->iomem=(uint8_t *)s->sram+off;
    bfr->is_mapped=true;
    return 0;
}
static const struct apple_rtkit_ops ops={.shmem_setup=setup};
struct sf_cio_rtkit *sf_cio_rtkit_create(void *entry,void *sram,uint64_t phys,size_t span)
{
    if(!entry || !sram || !phys || !span || phys>UINT64_MAX-span)return NULL;
    sf_rtkit_enter();
    struct sf_cio_rtkit *s=kzalloc(sizeof(*s),GFP_KERNEL);
    if(!s)return NULL;
    s->sram=sram;s->phys=phys;s->span=span;
    s->dev=sf_rtkit_device_create("acio1",entry);
    if(!s->dev){kfree(s);return NULL;}
    /* One stable device/context per boot. A failed boot keeps its mailbox and
     * SRAM context until a successful stop; never recycle a live cookie. */
    return s;
}
int sf_cio_rtkit_boot(struct sf_cio_rtkit *s)
{
    if(!s)return -EINVAL;
    sf_rtkit_enter();
    if(s->rtk)return -EBUSY;
    memset(s->ack,0,sizeof(s->ack)); // Previous engine/work was actually joined.
    int ret=sf_apple_mbox_bind(s->dev,0,"/arm-io/acio-cpu1");
    if(ret)return ret;
    struct apple_rtkit *rtk=apple_rtkit_init(s->dev,s,NULL,0,&ops);
    __atomic_store_n(&s->rtk,rtk,__ATOMIC_RELEASE);
    if(IS_ERR(s->rtk)){ret=PTR_ERR(s->rtk);s->rtk=NULL;return ret;}
    /* Keep the handle even after failure: the caller must drain it before
     * changing power/PHY. Boot does not imply NHI/router readiness. */
    return apple_rtkit_boot(s->rtk);
}
int sf_cio_rtkit_stop(struct sf_cio_rtkit *s)
{
    if(!s)return -EINVAL;
    sf_rtkit_enter();
    if(!s->rtk)return 0;
    int ret=apple_rtkit_poweroff(s->rtk);
    if(ret)return ret;
    apple_rtkit_free(s->rtk); // masks/joins mailbox receive work before freeing
    s->rtk=NULL;
    return 0;
}
