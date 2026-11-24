/* SPDX-License-Identifier: MIT */
/* Compile the actual source function against an interleaving/MMIO recorder.
 * This is a hosted state-machine test, never a hardware or DMA simulation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include SF_IRQ_UNIT_HEADER

static struct sf_avd_device device;
static struct sf_avd_session previous, next;
static unsigned char mailbox[0x100];
static unsigned lock_depth, wakes, submits, failures, cases;
static bool inject_publication, deferred_publication, reuse_session;
static uint32_t message;

static void publish_next(void) {
    struct sf_avd_session *s=reuse_session?&previous:&next;
    device.current=s;device.avd.job_ctx=&s->ctx;
    device.avd.job_seq++;device.avd.job_state=AVD_JOB_SUBMITTED;
    device.avd.job_end_sent=true;device.done=false;device.avd.job_pending=false;
}
unsigned long sf_avd_os_lock(void *lock) {
    assert(lock==&lock_depth&&lock_depth==0);lock_depth=1;return 0;
}
void sf_avd_os_unlock(void *lock,unsigned long flags) {
    (void)flags;assert(lock==&lock_depth&&lock_depth==1);lock_depth=0;
    if(deferred_publication){deferred_publication=false;publish_next();}
}
uint32_t sf_avd_os_readl(const void *address) {
    assert(address==mailbox+AVD_REG_MBOX1_RETRIEVE);
    /* A competing decode thread wants to publish its next job after the
     * old mailbox word was read. The same real lock serializes publication. */
    if(inject_publication){
        inject_publication=false;
        if(lock_depth)deferred_publication=true;else publish_next();
    }
    return message;
}
void sf_avd_os_writel(uint32_t value,void *address) {
    (void)value;
    assert(address==mailbox+AVD_REG_MBOX_IRQ_CLR||address==mailbox+AVD_REG_MBOX_IRQ_ENABLE);
}
void sf_avd_os_job_wake(struct sf_avd_device *dev) {assert(dev==&device);wakes++;}
static void submit(struct avd_ctx *ctx) {assert(ctx==&previous.ctx);submits++;}
static const struct avd_coded_fmt_ops operations={.submit=submit};
static const struct avd_coded_fmt_desc format={.ops=&operations};

static void reset(enum avd_job_state state,bool end_sent,uint32_t status) {
    assert(lock_depth==0);
    memset(&device,0,sizeof(device));memset(&previous,0,sizeof(previous));memset(&next,0,sizeof(next));
    device.avd.mbox=mailbox;device.avd.job_lock.os=&lock_depth;
    previous.ctx.coded_fmt_desc=next.ctx.coded_fmt_desc=&format;
    device.current=&previous;device.avd.job_ctx=&previous.ctx;
    device.avd.job_state=state;device.avd.job_end_sent=end_sent;device.avd.job_seq=1;
    inject_publication=deferred_publication=reuse_session=false;
    wakes=submits=0;message=status;
}
static void check(bool result,const char *name) {
    cases++;if(!result)failures++;
    printf("AVD_IRQ_SOURCE %s %s\n",result?"PASS":"FAIL",name);
}
static void race(bool same_session) {
    reset(AVD_JOB_FINISHING,true,0x1000);device.done=true;
    reuse_session=same_session;inject_publication=true;
    sf_avd_irq(&device);
    check(device.avd.job_seq==2&&device.avd.job_state==AVD_JOB_SUBMITTED&&
          device.current==(same_session?&previous:&next)&&!device.done&&
          wakes==0&&device.stats.stale==1,
          same_session?"old PP cannot finish next job in same session":"old PP cannot finish a different session");
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--race-only")){race(false);race(true);return failures?1:0;}
    reset(AVD_JOB_SUBMITTED,true,0x1000);sf_avd_irq(&device);
    check(device.done&&wakes==1&&device.avd.job_state==AVD_JOB_FINISHING,"normal submitted PP completes");
    reset(AVD_JOB_RUNNING,false,0x1000);sf_avd_irq(&device);
    check(!device.done&&wakes==0&&device.stats.stale==1,"PP before END remains stale");
    reset(AVD_JOB_RUNNING,true,0x1000);sf_avd_irq(&device);
    check(!device.done&&device.avd.job_pending&&wakes==0,"early PP after END remains pending");
    reset(AVD_JOB_SUBMITTED,true,0x100);sf_avd_irq(&device);
    check(submits==1&&!device.done&&wakes==0,"normal VP submits PP once");
    reset(AVD_JOB_SUBMITTED,true,0x1000);device.current=NULL;sf_avd_irq(&device);
    check(!device.done&&wakes==0&&device.stats.stale==1,"no owner cannot complete");
    reset(AVD_JOB_SUBMITTED,true,0x1000);device.avd.job_ctx=&next.ctx;sf_avd_irq(&device);
    check(!device.done&&wakes==0&&device.stats.stale==1,"mismatched context cannot complete");
    reset(AVD_JOB_SUBMITTED,true,0x10000);sf_avd_irq(&device);
    check(!device.done&&wakes==0&&device.stats.unhandled==1&&lock_depth==0,"debug branch unlocks");
    reset(AVD_JOB_SUBMITTED,true,0x2);sf_avd_irq(&device);
    check(!device.done&&wakes==0&&device.stats.errors==1&&lock_depth==0,"error branch unlocks");
    race(false);race(true);
    printf("AVD_IRQ_SOURCE cases=%u failures=%u hosted only; no kernel build/hardware/DMA proof\n",cases,failures);
    return failures?1:0;
}
