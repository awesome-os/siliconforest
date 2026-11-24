#include "bcm4378_fake_ring.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "test_bcm4378_fake_ring requires assertions"
#endif

static struct bct_session T;
static struct bcr_ring R, SAVED_R;
static uint8_t host_buffer[1024], saved_buffer[1024];
static const uint8_t ITEM[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}; /* synthetic only */
static const struct bcr_plan PLAN = {host_buffer, 32, 4, 8, UINT64_C(0x100000004)};

static struct {
    uint32_t phy, run, link, pci;
    unsigned calls, writes, bells, fail_at, apply_failed_write, drop_after_write, reset_after_write, reset_after_read;
    struct { unsigned region; uint32_t offset, value; } log[16];
} F;
static int no_write(void *ctx, unsigned r, uint32_t off, uint32_t value)
{ (void)ctx;(void)r;(void)off;(void)value; assert(!"default/kernel writer must not run"); return -1; }

#ifdef BCR_FAKE_DEVICE
#include "register_accessor.h"
static uint32_t config[0x1000/4], bar0[0x8000/4], bar2[0x10000/4];
static uint32_t saved_bar2[0x10000/4], saved_bar0[0x8000/4], saved_config[0x1000/4];
static struct apr_accessor A;
static struct apb_io IO;
static const struct bct_plan TRANSPORT_PLAN = {0x10000, 0x18000000, 14, 0xfeed0000};
static const uint32_t SH = BCT_RAM_BASE+0x100, INFO = BCT_RAM_BASE+0x300,
    DESC = BCT_RAM_BASE+0x400, WI = BCT_RAM_BASE+0x500, RI = BCT_RAM_BASE+0x504;
static uint32_t *slot(unsigned r, uint32_t off)
{
    assert(!(off & 3));
    if (r == APR_BCM_CONFIG) { assert(off < sizeof config); return &config[off/4]; }
    if (r == APR_BCM_BAR0) { assert(off < sizeof bar0); return &bar0[off/4]; }
    assert(r == APR_BCM_BAR2 && off >= BCT_RAM_BASE && off-BCT_RAM_BASE < sizeof bar2);
    return &bar2[(off-BCT_RAM_BASE)/4];
}
static int fake_read(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    (void)ctx; ++F.calls;
    if (F.calls == F.fail_at) { *out = UINT32_MAX; return -1; }
    if (r == APB_PHY) { assert(off == 0); *out = F.phy; }
    else if (r == APB_PORT0) { assert(off == 0x804 || off == 0x208); *out = off == 0x804 ? F.run : F.link; }
    else *out = *slot(r, off);
    if (F.calls == F.reset_after_read) F.phy |= 0x80;
    return 0;
}
static int fake_write(void *ctx, unsigned r, uint32_t off, uint32_t value)
{
    (void)ctx; ++F.calls;
    assert(F.phy == 0xf && F.run == 1 && F.link == 1);
    assert((r == APR_BCM_CONFIG && off == 0x80) ||
           (r == APR_BCM_BAR2 && (off == DESC+4 || off == DESC+8 || off == DESC+12 || off == WI)) ||
           (r == APR_BCM_BAR0 && (off == 0x2140 || off == 0x2a20)));
    assert(F.writes < 16);
    F.log[F.writes].region = r; F.log[F.writes].offset = off; F.log[F.writes].value = value;
    F.writes++;
    int fail = F.calls == F.fail_at;
    if (!fail || F.apply_failed_write) {
        if (r == APR_BCM_BAR0) {
            assert(value == 1 && !memcmp(host_buffer + ((R.write_index+3)%4)*8, ITEM, 8));
            assert((*slot(APR_BCM_BAR2, WI) & 0xffffu) == R.write_index);
            F.bells++;
        }
        *slot(r, off) = value;
    }
    if (F.writes == F.drop_after_write) F.link = 0;
    if (F.writes == F.reset_after_write) F.phy |= 0x80;
    return fail ? -1 : 0;
}
static void base(unsigned dar)
{
    (void)no_write; /* default-only trap is intentionally present in all builds */
    memset(&F, 0, sizeof F); memset(&T, 0, sizeof T); memset(&R, 0, sizeof R);
    memset(config, 0, sizeof config); memset(bar0, 0, sizeof bar0); memset(bar2, 0, sizeof bar2);
    memset(host_buffer, 0xa5, sizeof host_buffer);
    F.phy = 0xf; F.run = F.link = 1; F.pci = 0x442514e4; config[0] = F.pci; bar0[0] = 0x12345678;
    *slot(APR_BCM_BAR2, BCT_RAM_BASE+0x10000-4) = SH;
    *slot(APR_BCM_BAR2, SH) = 0x02000007u | (dar ? 0x80000000u : 0);
    *slot(APR_BCM_BAR2, SH+40) = BCT_RAM_BASE+0x200;
    *slot(APR_BCM_BAR2, SH+44) = BCT_RAM_BASE+0x204;
    *slot(APR_BCM_BAR2, SH+48) = INFO;
    *slot(APR_BCM_BAR2, INFO) = DESC; *slot(APR_BCM_BAR2, INFO+4) = WI; *slot(APR_BCM_BAR2, INFO+8) = RI;
    *slot(APR_BCM_BAR2, WI) = 0xface0000; *slot(APR_BCM_BAR2, RI) = 0xbeef0000;
    struct apb_io raw = {NULL, fake_read, fake_write, NULL, NULL, NULL};
    assert(apr_bind_bcm_fake(&A, &raw, &IO) == APB_OK);
    assert(bct_init(&T, &IO, &TRANSPORT_PLAN) == BCT_FAKE_OK);
    assert(bct_identify(&T) == BCT_FAKE_OK && bct_select_window(&T) == BCT_FAKE_OK && bct_discover_shared(&T) == BCT_FAKE_OK);
    assert(T.phase == BCT_SHARED);
    /* Begin measuring the NEW ring operations after an unchanged healthy fake
     * transport prefix; never hide prior writes in a runtime/hardware claim. */
    F.calls = F.writes = F.bells = 0; memset(F.log, 0, sizeof F.log);
}
static void prepare(unsigned op)
{
    base(0);
    if (op) assert(bcr_open(&R, &T, &PLAN) == BCR_FAKE_OK);
    if (op >= 2) assert(bcr_configure(&R) == BCR_FAKE_OK);
}
static void save(void)
{
    memcpy(&SAVED_R, &R, sizeof R); memcpy(saved_buffer, host_buffer, sizeof host_buffer);
    memcpy(saved_bar2, bar2, sizeof bar2); memcpy(saved_bar0, bar0, sizeof bar0); memcpy(saved_config, config, sizeof config);
}
static void unchanged(int ring)
{
    if (ring) assert(!memcmp(&SAVED_R, &R, sizeof R));
    assert(!memcmp(saved_buffer, host_buffer, sizeof host_buffer));
    assert(!memcmp(saved_bar2, bar2, sizeof bar2) && !memcmp(saved_bar0, bar0, sizeof bar0) && !memcmp(saved_config, config, sizeof config));
}
static int step(unsigned op)
{
    switch (op) {
    case 0: return bcr_open(&R, &T, &PLAN);
    case 1: return bcr_configure(&R);
    case 2: return bcr_submit(&R, ITEM, sizeof ITEM);
    default: return bcr_close(&R);
    }
}
static void configure_and_wrap(void)
{
    for (unsigned dar = 0; dar < 2; ++dar) {
        base(dar); assert(bcr_open(&R, &T, &PLAN) == BCR_FAKE_OK);
        assert(bcr_configure(&R) == BCR_FAKE_OK);
        assert(F.writes == 3 && F.bells == 0 && R.bell_offset == (dar ? 0x2a20u : 0x2140u));
        assert(F.log[0].offset == DESC+8 && F.log[0].value == 4);
        assert(F.log[1].offset == DESC+12 && F.log[1].value == 1);
        assert(F.log[2].offset == DESC+4 && F.log[2].value == (8u<<16 | 4u));
        assert(*slot(APR_BCM_BAR2, DESC) == 0);
        for (unsigned i = 0; i < 3; ++i) assert(bcr_submit(&R, ITEM, 8) == BCR_FAKE_OK);
        assert(R.write_index == 3 && F.bells == 3 && R.completed == 3);
        assert(*slot(APR_BCM_BAR2, WI) == 0xface0003 && *slot(APR_BCM_BAR2, RI) == 0xbeef0000);
        save(); unsigned before = F.writes;
        assert(bcr_submit(&R, ITEM, 8) == BCR_FULL && F.writes == before); unchanged(1);
        assert(bcr_close(&R) == BCR_BUSY);
        *slot(APR_BCM_BAR2, RI) = 0xbeef0002; /* synthetic consumer, never DMA */
        assert(bcr_submit(&R, ITEM, 8) == BCR_FAKE_OK && R.write_index == 0);
        assert(!memcmp(host_buffer+24, ITEM, 8) && *slot(APR_BCM_BAR2, WI) == 0xface0000);
        *slot(APR_BCM_BAR2, RI) = 0xbeef0000;
        save(); before = F.writes;
        assert(bcr_close(&R) == BCR_FAKE_OK && R.closed && !R.configured && F.writes == before);
        unchanged(0); /* descriptor and buffer retained after logical close */
        assert(bcr_open(&R, &T, &PLAN) == BCR_STATE);
    }
}
static void unsafe_states_zero_writes(void)
{
    for (unsigned op = 0; op < 4; ++op) for (unsigned bad = 0; bad < 7; ++bad) {
        prepare(op); save(); unsigned before = F.writes, calls = F.calls;
        if (bad == 0) F.phy = 0x8f; else if (bad == 1) F.phy = 0x80;
        else if (bad == 2) F.link = 0; else if (bad == 3) F.run = 0;
        else if (bad == 4) F.link = 5; else if (bad == 5) F.link = 0x41; else F.phy = 0xb;
        assert(step(op) == (bad == 0 || bad == 1 || bad == 6 ? BCR_PHY_NOT_READY : BCR_LINK_DOWN));
        assert(F.writes == before);
        if (bad == 0 || bad == 1 || bad == 6) assert(F.calls == calls+1);
        unchanged(1);
    }
    for (unsigned op = 0; op < 4; ++op) {
        prepare(op); save(); unsigned before = F.writes;
        F.reset_after_read = F.calls+2;
        assert(step(op) == BCR_PHY_NOT_READY && F.writes == before); unchanged(1);
    }
    prepare(2); save(); unsigned before = F.writes;
    /* 5 entry reads + 2*(5 guard+1 index) =17, then reset after the
     * second read of the LAST guard before the host-buffer copy. */
    F.reset_after_read = F.calls+19;
    assert(bcr_submit(&R, ITEM, 8) == BCR_PHY_NOT_READY && F.writes == before); unchanged(1);
}
static void invalid_metadata_and_ownership(void)
{
    base(0); *slot(APR_BCM_BAR2, INFO+4) = DESC+4;
    assert(bcr_open(&R, &T, &PLAN) == BCR_RANGE && !R.bound && F.writes == 0);
    base(0); *slot(APR_BCM_BAR2, INFO+8) = WI;
    assert(bcr_open(&R, &T, &PLAN) == BCR_RANGE);
    base(0); *slot(APR_BCM_BAR2, INFO) = SH;
    assert(bcr_open(&R, &T, &PLAN) == BCR_RANGE);
    base(0); *slot(APR_BCM_BAR2, INFO) = SH+64;
    assert(bcr_open(&R, &T, &PLAN) == BCR_RANGE);
    base(0); T.ring_info_address = SH+80; *slot(APR_BCM_BAR2, SH+48) = T.ring_info_address;
    assert(bcr_open(&R, &T, &PLAN) == BCR_RANGE);
    base(0); *slot(APR_BCM_BAR2, INFO) = UINT32_MAX;
    assert(bcr_open(&R, &T, &PLAN) == BCR_RANGE);
    base(0); T.shared_flags |= 0x10000; *slot(APR_BCM_BAR2, SH) = T.shared_flags;
    assert(bcr_open(&R, &T, &PLAN) == BCR_STATE);
    base(0); *slot(APR_BCM_BAR2, SH+48) += 4;
    assert(bcr_open(&R, &T, &PLAN) == BCR_OWNER);
    base(0); struct bcr_plan p = PLAN; p.capacity++;
    assert(bcr_open(&R, &T, &p) == BCR_RANGE);
    p = PLAN; p.fake_bus_label = UINT64_MAX-3;
    assert(bcr_open(&R, &T, &p) == BCR_RANGE);
    prepare(1); *slot(APR_BCM_BAR2, DESC+4) = 1; save(); unsigned before = F.writes;
    assert(bcr_configure(&R) == BCR_OWNER && F.writes == before); unchanged(1);
    prepare(2); save(); before = F.writes;
    assert(bcr_submit(&R, ITEM, 7) == BCR_ARGUMENT && F.writes == before); unchanged(1);
    assert(bcr_submit(&R, host_buffer, 8) == BCR_ARGUMENT); unchanged(1);
    *slot(APR_BCM_BAR2, RI) = 0xbeef0004;
    assert(bcr_submit(&R, ITEM, 8) == BCR_VERIFY && R.faulted);
    prepare(2); *slot(APR_BCM_BAR2, WI) = 0xface0001;
    assert(bcr_submit(&R, ITEM, 8) == BCR_VERIFY && R.faulted);
}
static void partial_failure_no_retry(void)
{
    prepare(1); F.reset_after_write = F.writes+1;
    assert(bcr_configure(&R) == BCR_PHY_NOT_READY && R.faulted && R.dirty && !R.configured);
    assert(F.writes == 1 && *slot(APR_BCM_BAR2, DESC+8) == 4 && *slot(APR_BCM_BAR2, DESC+12) == 0);
    unsigned before = F.writes;
    assert(bcr_configure(&R) == BCR_FAULTED && F.writes == before);
    prepare(2); F.drop_after_write = F.writes+1;
    assert(bcr_submit(&R, ITEM, 8) == BCR_LINK_DOWN && R.faulted && R.dirty);
    assert(*slot(APR_BCM_BAR2, WI) == 0xface0001 && F.bells == 0 && R.completed == 0);
    before = F.writes;
    assert(bcr_submit(&R, ITEM, 8) == BCR_FAULTED && F.writes == before);
}
static void all_callback_errors(void)
{
    prepare(0);
    assert(bcr_open(&R, &T, &PLAN) == BCR_FAKE_OK && bcr_configure(&R) == BCR_FAKE_OK && bcr_submit(&R, ITEM, 8) == BCR_FAKE_OK);
    *slot(APR_BCM_BAR2, RI) = 0xbeef0001;
    assert(bcr_close(&R) == BCR_FAKE_OK);
    unsigned total = F.calls;
    for (unsigned applied = 0; applied < 2; ++applied) for (unsigned fail = 1; fail <= total; ++fail) {
        prepare(0); F.fail_at = fail; F.apply_failed_write = applied;
        int e = bcr_open(&R, &T, &PLAN);
        if (!e) e = bcr_configure(&R);
        if (!e) e = bcr_submit(&R, ITEM, 8);
        if (!e) { *slot(APR_BCM_BAR2, RI) = 0xbeef0001; e = bcr_close(&R); }
        assert(e == BCR_IO && F.calls == fail);
        if (R.bound) {
            assert(R.faulted);
            unsigned calls = F.calls;
            assert(bcr_submit(&R, ITEM, 8) == BCR_FAULTED && F.calls == calls);
        }
    }
    printf("HOST callback boundaries: %u (both failed-write application models)\n", total);
}
#else
static void default_zero_io(void)
{
    memset(&R, 0, sizeof R); memset(&T, 0, sizeof T); memset(host_buffer, 0xa5, sizeof host_buffer);
    memcpy(&SAVED_R, &R, sizeof R); memcpy(saved_buffer, host_buffer, sizeof host_buffer);
    T.io.write32 = no_write;
    assert(!bcr_fake_ring_compiled());
    assert(bcr_open(&R, &T, &PLAN) == BCR_GATED);
    assert(bcr_configure(&R) == BCR_GATED && bcr_submit(&R, ITEM, 8) == BCR_GATED && bcr_close(&R) == BCR_GATED);
    assert(!memcmp(&R, &SAVED_R, sizeof R) && !memcmp(host_buffer, saved_buffer, sizeof host_buffer) && !F.calls && !F.writes);
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BCR_FAKE_DEVICE
    assert(bcr_fake_ring_compiled());
    configure_and_wrap(); unsafe_states_zero_writes(); invalid_metadata_and_ownership(); partial_failure_no_retry(); all_callback_errors();
#else
    default_zero_io();
#endif
    puts("HOST ONLY PASS: fake ring descriptor/index/doorbell; no allocation, DMA or hardware");
    return 0;
}
