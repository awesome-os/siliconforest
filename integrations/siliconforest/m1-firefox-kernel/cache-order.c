/* SPDX-License-Identifier: MIT */
/* Actual decode/grey functions with recorded OS/codec boundaries. No fake DMA proof. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include SF_CACHE_UNIT_HEADER

static struct sf_avd_device device;
static struct sf_avd_session session;
static struct avd_decoded_buffer capture;
static unsigned char pixels[128], mailbox[0x4000];
static unsigned events[32], count, lock_depth;
static int mode, fail_phase;
static bool grey;
enum { NORMAL, EARLY, HELD, RUN_ERROR, TIMEOUT, DEVICE_ERROR };
static void event(unsigned e) { assert(count < 32); events[count++] = e; }
unsigned long sf_avd_os_lock(void *p) { assert(p == &lock_depth && !lock_depth); lock_depth++; return 0; }
void sf_avd_os_unlock(void *p, unsigned long f) { (void)f; assert(p == &lock_depth && lock_depth == 1); lock_depth--; }
uint64_t sf_avd_os_now_us(void) { return 100; }
void sf_avd_os_log(int level, const char *format, ...) { (void)level; (void)format; }
void sf_avd_os_writel(uint32_t v, void *a) { (void)v; assert(a == mailbox + AVD_REG_MBOX_IRQ_ENABLE); }
void avd_status(struct avd_dev *a, u32 vp) { assert(a == &device.avd && vp <= 2); }
static int avd_reset(struct sf_avd_device *d) { assert(d == &device); event(70); return -EIO; }
int sf_avd_session_buffer(struct sf_avd_session *s, unsigned i, void **v, size_t *z, void **cookie) {
    assert(s == &session && i == 0); *v = pixels; *z = sizeof(pixels); *cookie = &capture; return 0;
}
int sf_avd_os_dma_sync(void *cookie, enum sf_avd_dma_phase phase) {
    assert(cookie == &capture && !lock_depth);
    event(phase == SF_AVD_DMA_BEFORE_DEVICE ? 10 : phase == SF_AVD_DMA_AFTER_DEVICE ? 50 : 60);
    if (phase == SF_AVD_DMA_AFTER_DEVICE) {
        assert(session.m2m.sf_finished && session.m2m.sf_result == VB2_BUF_STATE_DONE);
        /* If grey_chroma ran first, this byte would already be 0x80. */
        assert(pixels[64] == 0x22);
    } else if (phase == SF_AVD_DMA_PUBLISH_CPU) {
        assert(grey && pixels[64] == 0x80 && pixels[95] == 0x80);
    } else assert(phase == SF_AVD_DMA_BEFORE_DEVICE);
    return (int)phase == fail_phase ? -EIO : 0;
}
void avd_job_finish(struct avd_ctx *ctx, enum vb2_buffer_state result) {
    assert(ctx == &session.ctx); event(40); session.m2m.sf_finished = true;
    session.m2m.sf_result = result; device.avd.job_state = AVD_JOB_IDLE;
}
static int run(struct avd_ctx *ctx) {
    assert(ctx == &session.ctx && !lock_depth); event(20);
    capture.grey_chroma_offset = grey ? 64 : 0;
    if (mode == RUN_ERROR) return -EINVAL;
    if (mode == HELD) avd_job_finish(ctx, VB2_BUF_STATE_DONE);
    if (mode == EARLY) { device.avd.job_pending = true; device.avd.job_pending_result = VB2_BUF_STATE_DONE; }
    return 0;
}
bool sf_avd_os_job_wait(struct sf_avd_device *dev, uint32_t ms) {
    assert(dev == &device && ms == 2000 && !lock_depth); event(30);
    if (mode == TIMEOUT) return false;
    dev->done = true;
    dev->done_state = mode == DEVICE_ERROR ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE;
    return true;
}
static const struct avd_coded_fmt_ops operations = { .run = run };
static const struct avd_coded_fmt_desc format = { .ops = &operations };
static void reset(bool imported, int m, bool g, int fail) {
    memset(&device, 0, sizeof(device)); memset(&session, 0, sizeof(session));
    memset(&capture, 0, sizeof(capture)); memset(pixels, 0x22, sizeof(pixels));
    assert(!lock_depth); count = 0; mode = m; grey = g; fail_phase = fail;
    device.booted = true; device.avd.job_lock.os = &lock_depth; device.avd.mbox = mailbox;
    session.dev = &device; session.started = true; session.imported = imported;
    session.ctx.coded_fmt_desc = &format; session.cap[0] = &capture; session.ncap = 1;
    session.coded.cpu = pixels; session.coded.size = sizeof(pixels);
    capture.base.vb.vb2_buf.sf_vaddr = pixels;
}
static unsigned cases;
static void check(const char *name, int expected, const unsigned *want, size_t n) {
    uint32_t status = 123;
    int got = sf_avd_session_decode(&session, 10, 0, 123, mode == HELD ? SFAVD_DECODE_HOLD : 0, &status);
    assert(got == expected && count == n && !lock_depth);
    assert(!memcmp(events, want, n * sizeof(*want)));
    if (got) assert(device.stats.done == 0);
    cases++; printf("AVD_CACHE_ORDER PASS %s\n", name);
}
#define CHECK(name, result, ...) do { const unsigned e[] = { __VA_ARGS__ }; check(name, result, e, sizeof(e)/sizeof(e[0])); } while (0)
int main(void) {
    reset(true,NORMAL,false,0); CHECK("completed output visibility",0,10,20,30,40,50);
    reset(true,EARLY,false,0); CHECK("completion before wait",0,10,20,40,50);
    reset(true,NORMAL,true,0); CHECK("completion before grey fill before publish",0,10,20,30,40,50,60);
    reset(true,HELD,false,0); CHECK("HOLD is not device completion",0,10,20,40);
    reset(false,NORMAL,false,0); CHECK("legacy path unchanged",0,20,30,40);
    reset(true,NORMAL,false,SF_AVD_DMA_BEFORE_DEVICE); CHECK("pre-handoff failure never submits",-EIO,10);
    reset(true,TIMEOUT,false,0); CHECK("timeout/reset failure never reads output",-ETIMEDOUT,10,20,30,70,40);
    reset(true,DEVICE_ERROR,false,0); CHECK("error completion never reads output",-EIO,10,20,30,40);
    reset(true,RUN_ERROR,false,0); CHECK("codec rejection never reads output",-EINVAL,10,20,40);
    reset(true,NORMAL,true,SF_AVD_DMA_AFTER_DEVICE); CHECK("failed completion sync never fills grey",-EIO,10,20,30,40,50);
    reset(true,NORMAL,true,SF_AVD_DMA_PUBLISH_CPU); CHECK("failed grey publish never reports frame done",-EIO,10,20,30,40,50,60);
    reset(true,NORMAL,false,0); device.booted=false; CHECK("unbooted never hands off",-ENODEV);
    printf("AVD_CACHE_ORDER %u actual-source cases PASS (host only)\n",cases);
    return 0;
}
