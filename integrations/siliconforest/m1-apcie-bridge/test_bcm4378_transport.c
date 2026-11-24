#include "bcm4378_transport.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "test_bcm4378_transport requires assertions"
#endif

#ifdef BCT_FAKE_BAR_FILE
#include "register_accessor.h"
static uint32_t config[APR_BCM_CONFIG_BYTES/4], bar0[APR_BCM_BAR0_BYTES/4], bar2[APR_BCM_BAR2_BYTES/4];
static struct {
    unsigned link, run, calls, reads, writes, fail_at, drop_after_write, kicks, bad_window;
    struct { unsigned space; uint32_t offset, value; } log[16];
} F;
static struct apr_accessor A;
static struct apb_io IO;
static const struct bct_plan PLAN = {0x10000, 0x18000000, 14, 0xfeed0000};
static const uint32_t shared = BCT_RAM_BASE + 0x100, h2d = BCT_RAM_BASE + 0x200,
    d2h = BCT_RAM_BASE + 0x204, ringinfo = BCT_RAM_BASE + 0x300;

static uint32_t *slot(unsigned r, uint32_t off)
{
    assert(!(off & 3));
    if (r == APR_BCM_CONFIG) { assert(off < sizeof config); return &config[off/4]; }
    if (r == APR_BCM_BAR0) { assert(off < sizeof bar0); return &bar0[off/4]; }
    assert(r == APR_BCM_BAR2 && off < sizeof bar2); return &bar2[off/4];
}
static int raw_read(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    (void)ctx; F.calls++; F.reads++;
    if (F.calls == F.fail_at) { *out = UINT32_MAX; return -1; }
    if (r == APB_PORT0) {
        assert(off == 0x804 || off == 0x208);
        *out = off == 0x804 ? F.run : F.link;
    } else *out = *slot(r, off);
    return 0;
}
static int raw_write(void *ctx, unsigned r, uint32_t off, uint32_t v)
{
    (void)ctx; F.calls++;
    assert(F.run && (F.link & 0x45) == 1);
    assert(F.writes < 16);
    F.log[F.writes].space = r; F.log[F.writes].offset = off; F.log[F.writes].value = v;
    F.writes++;
    if (F.calls == F.fail_at) return -1;
    assert((r == APR_BCM_CONFIG && (off == 0x80 || off == 0x98)) ||
           (r == APR_BCM_BAR0 && (off == 0x204c || off == 0x2c34)) ||
           (r == APR_BCM_BAR2 && off == h2d));
    *slot(r, off) = (F.bad_window && r == APR_BCM_CONFIG && off == 0x80) ? v ^ 0x1000 : v;
    if (r == APR_BCM_CONFIG && off == 0x98) { assert(v == 1); F.kicks++; }
    if (F.writes == F.drop_after_write) F.link = 0;
    return 0;
}
static void reset(struct bct_session *s)
{
    memset(&F, 0, sizeof F); memset(config, 0, sizeof config);
    memset(bar0, 0, sizeof bar0); memset(bar2, 0, sizeof bar2); memset(s, 0, sizeof *s);
    F.run = F.link = 1;
    config[0] = 0x442514e4; bar0[0] = 0x12345678; /* raw synthetic chip word, not chip identity */
    bar0[0x204c/4] = bar0[0x2c34/4] = UINT32_MAX;
    bar2[(BCT_RAM_BASE + PLAN.ram_bytes - 4)/4] = shared;
    bar2[shared/4] = 0x02000007; bar2[(shared+40)/4] = h2d;
    bar2[(shared+44)/4] = d2h; bar2[(shared+48)/4] = ringinfo;
    bar2[d2h/4] = 0xabcd;
    struct apb_io raw = {NULL, raw_read, raw_write, NULL, NULL, NULL};
    assert(apr_bind_bcm_fake(&A, &raw, &IO) == APB_OK);
    assert(bct_init(s, &IO, &PLAN) == BCT_FAKE_OK && F.calls == 0);
}
static int step(struct bct_session *s, unsigned n)
{
    switch (n) {
    case 0: return bct_identify(s);
    case 1: return bct_select_window(s);
    case 2: return bct_discover_shared(s);
    case 3: return bct_setup_mailbox(s);
    default: return bct_post_mailbox(s, 0x1234); /* opaque synthetic word */
    }
}
static void advance(struct bct_session *s, unsigned n)
{ for (unsigned i = 0; i < n; ++i) assert(step(s, i) == BCT_FAKE_OK); }

static void full_flow(void)
{
    struct bct_session s;
    for (unsigned revision = 13; revision <= 64; revision += 1) {
        if (revision != 13 && revision != 14 && revision != 64) continue;
        reset(&s); s.plan.pcie_core_revision = revision; advance(&s, 5);
        assert(s.phase == BCT_POSTED && !s.faulted && s.chip_word == 0x12345678);
        assert(s.shared_address == shared && s.h2d_address == h2d && s.ring_info_address == ringinfo);
        assert(s.d2h_word == 0xabcd && bar2[d2h/4] == 0xabcd);
        assert(F.kicks == (revision <= 13 ? 2u : 1u));
        assert(F.writes == 3 + F.kicks && bar2[h2d/4] == 0x1234);
        assert(F.log[0].space == APR_BCM_CONFIG && F.log[0].offset == 0x80);
        assert(F.log[1].space == APR_BCM_BAR0 && F.log[1].offset == (revision >= 64 ? 0x2c34u : 0x204cu) && F.log[1].value == 0);
        assert(F.log[2].space == APR_BCM_BAR2 && F.log[2].offset == h2d);
        unsigned writes = F.writes;
        assert(bct_post_mailbox(&s, 1) == BCT_ORDER && F.writes == writes);
    }
}
static void down_refuses_every_step(void)
{
    struct bct_session s;
    for (unsigned n = 0; n < 5; ++n) {
        for (unsigned variant = 0; variant < 4; ++variant) {
            reset(&s); advance(&s, n);
            unsigned writes = F.writes;
            if (variant == 0) F.run = 0;
            else F.link = variant == 1 ? 0 : variant == 2 ? 5 : 0x41;
            assert(step(&s, n) == BCT_LINK_DOWN && F.writes == writes);
        }
    }
    reset(&s); advance(&s, 4);
    F.drop_after_write = F.writes + 1;
    assert(bct_post_mailbox(&s, 0x1234) == BCT_LINK_DOWN);
    assert(F.kicks == 0 && s.faulted && s.dirty && bar2[h2d/4] == 0x1234);
    unsigned writes = F.writes;
    assert(bct_post_mailbox(&s, 0x1234) == BCT_FAULTED && F.writes == writes);
}
static void malformed_and_busy(void)
{
    struct bct_session s;
    reset(&s); config[0] = 0x5f6914e4;
    assert(bct_identify(&s) == BCT_IDENTITY && F.writes == 0);
    reset(&s); advance(&s, 1); F.bad_window = 1;
    assert(bct_select_window(&s) == BCT_VERIFY && s.faulted);
    uint32_t badptrs[] = {0, PLAN.initial_tail, BCT_RAM_BASE-4, BCT_RAM_BASE+2,
                         BCT_RAM_BASE+PLAN.ram_bytes-4, UINT32_MAX};
    for (unsigned i = 0; i < sizeof badptrs/sizeof badptrs[0]; ++i) {
        reset(&s); advance(&s, 2); unsigned before = F.writes;
        bar2[(BCT_RAM_BASE+PLAN.ram_bytes-4)/4] = badptrs[i];
        assert(bct_discover_shared(&s) == (i < 2 ? BCT_WAIT_FIRMWARE : BCT_RANGE));
        assert(F.writes == before);
    }
    for (unsigned field = 40; field <= 48; field += 4) {
        reset(&s); advance(&s, 2); bar2[(shared+field)/4] = shared;
        assert(bct_discover_shared(&s) == BCT_RANGE);
    }
    reset(&s); advance(&s, 2); bar2[(shared+44)/4] = h2d;
    assert(bct_discover_shared(&s) == BCT_RANGE);
    for (unsigned flags = 4; flags <= 8; ++flags) {
        reset(&s); advance(&s, 2); bar2[shared/4] = flags;
        assert(bct_discover_shared(&s) == (flags == 5 ? BCT_FAKE_OK : BCT_UNSUPPORTED));
    }
    reset(&s); advance(&s, 3); bar2[h2d/4] = 1; unsigned before = F.writes;
    assert(bct_setup_mailbox(&s) == BCT_BUSY && F.writes == before);
    reset(&s); advance(&s, 4); bar2[h2d/4] = 1; before = F.writes;
    assert(bct_post_mailbox(&s, 0x1234) == BCT_BUSY && F.writes == before);
    reset(&s); advance(&s, 4); before = F.writes;
    assert(bct_post_mailbox(&s, 0) == BCT_ARGUMENT && F.writes == before);
}
static void every_callback_fault(void)
{
    struct bct_session s;
    reset(&s); advance(&s, 5); unsigned total = F.calls;
    for (unsigned n = 1; n <= total; ++n) {
        reset(&s); F.fail_at = n;
        int e = 0;
        for (unsigned i = 0; i < 5 && !e; ++i) e = step(&s, i);
        assert(e == BCT_IO && F.calls == n && s.faulted);
        unsigned before = F.calls;
        assert(bct_identify(&s) == BCT_FAULTED && F.calls == before);
    }
    printf("HOST fault boundaries: %u\n", total);
}
static void accessor_and_plan_rejections(void)
{
    struct bct_session s;
    reset(&s);
    struct bct_plan p = PLAN;
    struct bct_session empty = {0};
    assert(bct_init(&s, &IO, &p) == BCT_ORDER);
    p.ram_bytes = UINT32_MAX; assert(bct_init(&empty, &IO, &p) == BCT_RANGE);
    p = PLAN; p.window_page = 0x1001; assert(bct_init(&empty, &IO, &p) == BCT_RANGE);
    p = PLAN; p.pcie_core_revision = UINT32_MAX; assert(bct_init(&empty, &IO, &p) == BCT_UNSUPPORTED);
    struct apb_io raw = {NULL, raw_read, raw_write, NULL, NULL, NULL}, legacy;
    struct apr_accessor a;
    assert(bct_init(&empty, &raw, &PLAN) == BCT_ARGUMENT);
    assert(apr_bind_fake(&a, &raw, &legacy) == APB_OK);
    uint32_t v = 0xaabbccdd;
    assert(legacy.read32(legacy.context, APR_BCM_CONFIG, 0, &v) == APB_ARGUMENT && v == 0xaabbccdd);
    unsigned before = F.calls;
    assert(IO.write32(IO.context, APR_BCM_CONFIG, 4, 7) == APB_GATED);
    assert(IO.read32(IO.context, APR_BCM_BAR0, APR_BCM_BAR0_BYTES, &v) == APB_ARGUMENT);
    assert(IO.read32(IO.context, APR_BCM_BAR2, UINT32_MAX-3, &v) == APB_ARGUMENT);
    assert(IO.read32(IO.context, APR_BCM_BAR2, 1, &v) == APB_ARGUMENT);
    assert(IO.write32(IO.context, APB_PMGR, 0x348, 1) == APB_GATED);
    assert(IO.write32(IO.context, APB_PHY, 0, 1) == APB_GATED);
    assert(F.calls == before);
}
#else
static unsigned callbacks;
static int forbidden_read(void *c, unsigned r, uint32_t o, uint32_t *v)
{ (void)c;(void)r;(void)o;(void)v; callbacks++; return -1; }
static int forbidden_write(void *c, unsigned r, uint32_t o, uint32_t v)
{ (void)c;(void)r;(void)o;(void)v; callbacks++; return -1; }
static void default_never_accesses(void)
{
    struct bct_session s = {0};
    struct apb_io io = {NULL, forbidden_read, forbidden_write, NULL, NULL, NULL};
    struct bct_plan p = {0};
    assert(!bct_fake_transport_compiled());
    assert(bct_init(&s, &io, &p) == BCT_GATED);
    assert(bct_identify(&s) == BCT_GATED);
    assert(bct_select_window(&s) == BCT_GATED);
    assert(bct_discover_shared(&s) == BCT_GATED);
    assert(bct_setup_mailbox(&s) == BCT_GATED);
    assert(bct_post_mailbox(&s, 1) == BCT_GATED);
    assert(callbacks == 0 && s.reads_attempted == 0 && s.writes_attempted == 0 && !s.dirty);
}
#endif
int main(void)
{
    assert(apb_shared_writes_compiled() == 0 && apl_shared_writes_compiled() == 0);
    assert(apg_fake_drive_compiled() == 0 && ars_fake_enable_compiled() == 0);
#ifdef BCT_FAKE_BAR_FILE
    assert(bct_fake_transport_compiled());
    full_flow(); down_refuses_every_step(); malformed_and_busy(); every_callback_fault(); accessor_and_plan_rejections();
#else
    default_never_accesses();
#endif
    puts("HOST ONLY PASS: BCM fake transport; no hardware, GPIO or SMC path");
    return 0;
}
