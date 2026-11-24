/* Host-only tests for bringup.c against a simulated register file. No hardware.
 * Fake mode:  cc -DAPB_FAKE_REGISTER_FILE ... bringup.c test_bringup.c
 * Gated mode: cc ...                       bringup.c test_bringup.c  (bundle-equivalent) */
#include "bringup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error test_bringup requires assertions
#endif

/* ---------- simulated register file ---------- */
static uint32_t mem_pmgr[0x4000 / 4], mem_rc[0x4000 / 4], mem_phy[0x10000 / 4], mem_phyip[0x20000 / 4],
    mem_axi[0x4000 / 4], mem_fuse[0x1000 / 4], mem_port0[0x8000 / 4], mem_ecam0[0x8000 / 4];
static uint32_t *const mem[APB_REGIONS] = {mem_pmgr, mem_rc, mem_phy, mem_phyip, mem_axi, mem_fuse,
                                           mem_port0, mem_ecam0};
struct wentry { unsigned region; uint32_t off, val; int width; };
static struct fake {
    int pmgr_stuck;            /* PMGR offset whose actual state never follows, 0 none */
    uint32_t phy_ack_stuck;    /* ACK bit that never follows its REQ, 0 none */
    int run_never, busy_reads, busy_forever;
    int fail_read_at, fail_write_at;
    unsigned reads, write_calls, linksts_reads, nw;
    uint64_t delayed;
    struct wentry log[4096];
} F;

static int phy_acks_ok(void)
{
    uint32_t v = mem_phy[0];
    return (v & 1) && (v & 2) && F.phy_ack_stuck == 0;
}
static int fake_read32(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    (void)ctx;
    assert(r < APB_REGIONS && !(off & 3) && off < apb_region_size(r));
    F.reads++;
    if (F.fail_read_at && (int)F.reads == F.fail_read_at) return -1;
    uint32_t v = mem[r][off / 4];
    if (r == APB_PMGR && (off == 0x1b8 || off == 0x348 || off == 0x3e8 || off == 0x1a0)) {
        uint32_t target = v & 0xf;
        v &= ~0xf0u;
        if ((int)off != F.pmgr_stuck) v |= target << 4;
    } else if (r == APB_PHY && off == 0) {
        v &= ~0xcu;
        if ((v & 1) && F.phy_ack_stuck != 4) v |= 4;
        if ((v & 2) && F.phy_ack_stuck != 8) v |= 8;
    } else if (r == APB_PORT0 && off == 0x804) {
        int run = (mem_port0[0x800 / 4] & 1) && (mem_port0[0x814 / 4] & 1) && (mem_rc[0x24 / 4] & 1) &&
                  !(mem_phy[0] & 0x80) && phy_acks_ok() && !F.run_never;
        v = (v & ~1u) | (run ? 1u : 0u);
    } else if (r == APB_PORT0 && off == 0x208) {
        F.linksts_reads++;
        int busy = F.busy_forever || (int)F.linksts_reads <= F.busy_reads;
        v = busy ? 0x4u : 0x1u;     /* BUSY while training, then UP */
    }
    *out = v;
    return 0;
}
static int fake_write32(void *ctx, unsigned r, uint32_t off, uint32_t val)
{
    (void)ctx;
    assert(r < APB_REGIONS && r != APB_FUSE && !(off & 3) && off < apb_region_size(r));
    F.write_calls++;
    if (F.fail_write_at && (int)F.write_calls == F.fail_write_at) return -1;
    /* Status bits are read-only, as on the hardware: PMGR actual state 7:4 and
     * PHY CTRL clock ACKs 2/3 are not stored by a write-back. */
    if (r == APB_PMGR) val &= ~0xf0u;
    if (r == APB_PHY && off == 0) val &= ~0xcu;
    mem[r][off / 4] = val;
    assert(F.nw < 4096);
    F.log[F.nw++] = (struct wentry){r, off, val, 32};
    return 0;
}
static int fake_read16(void *ctx, unsigned r, uint32_t off, uint16_t *out)
{
    (void)ctx;
    assert(r == APB_ECAM0 && off == 0xa0);
    F.reads++;
    *out = (uint16_t)(mem_ecam0[off / 4] & 0xffff);
    return 0;
}
static int fake_write16(void *ctx, unsigned r, uint32_t off, uint16_t val)
{
    (void)ctx;
    assert(r == APB_ECAM0 && off == 0xa0);
    F.write_calls++;
    mem_ecam0[off / 4] = (mem_ecam0[off / 4] & 0xffff0000u) | val;
    F.log[F.nw++] = (struct wentry){r, off, val, 16};
    return 0;
}
static void fake_delay(void *ctx, uint32_t us) { (void)ctx; F.delayed += us; }

static const struct apb_io IO = {NULL, fake_read32, fake_write32, fake_read16, fake_write16, fake_delay};

/* ---------- plan with synthetic tunables (shapes like the J293 ADT) ---------- */
static const struct apb_tunable T_AXI[2] = {{0x800, 0xff, 0x12}, {0x10, 0xf0, 0x30}};
static const struct apb_tunable T_COMMON[1] = {{0x54, 0xffff, 0x140}};
static const struct apb_tunable T_PHY[2] = {{0x8, 0x3, 0x1}, {0xc004, 0x100, 0x100}};    /* port-2 bank */
static const struct apb_tunable T_PLL[1] = {{0x62c0 - 4, 0xffffffffu, 0xdeadbeefu}};
static const struct apb_tunable T_AUSPMA[1] = {{0x1d024, 0x1, 0x1}};
static const struct apb_tunable T_CONFIG[1] = {{0x800, 0x100100, 0x100100}};
static const struct apb_tunable T_RC[1] = {{0xb84, 0xff00, 0x2200}};
static const struct apb_tunable T_GEN3[1] = {{0x8a8, 0x1, 0x1}};
static const struct apb_tunable T_GEN4[1] = {{0x8a8, 0x2, 0x2}};
static struct apb_plan plan(void)
{
    struct apb_plan p = {{T_AXI, 2}, {T_COMMON, 1}, {T_PHY, 2}, {T_PLL, 1}, {T_AUSPMA, 1},
                         {T_CONFIG, 1}, {T_RC, 1}, {T_GEN3, 1}, {T_GEN4, 1}, 2};
    return p;
}

static void reset_fake(void)
{
    for (unsigned r = 0; r < APB_REGIONS; r++) memset(mem[r], 0, apb_region_size(r));
    memset(&F, 0, sizeof F);
    /* Cold state: AUTO and WAS-gated bits set, target 0; PHY in reset (as observed). */
    mem_pmgr[0x1b8 / 4] = mem_pmgr[0x348 / 4] = mem_pmgr[0x3e8 / 4] = mem_pmgr[0x1a0 / 4] = 0x10000300u;
    mem_phy[0] = 0x80u;
    mem_fuse[0x84 / 4] = 0xa5a5a5a5u; mem_fuse[0x418 / 4] = 0x8c3e5d71u; mem_fuse[0x41c / 4] = 0x0000001bu;
    mem_ecam0[0xa0 / 4] = 0xabcd0000u;   /* Link Status 2 sentinel in the upper half */
    mem_phyip[0x522c / 4] = 0xffffffffu;
    mem_axi[0x10 / 4] = 0x0000000fu;
}
#ifdef APB_FAKE_REGISTER_FILE
static void count_classes(unsigned *shared, unsigned *port, int *last_shared, int *first_port)
{
    *shared = *port = 0; *last_shared = -1; *first_port = -1;
    for (unsigned i = 0; i < F.nw; i++) {
        if (apb_region_shared(F.log[i].region)) { (*shared)++; *last_shared = (int)i; }
        else { (*port)++; if (*first_port < 0) *first_port = (int)i; }
    }
}
#endif

static void parser_plan_and_speed(void)
{
    uint8_t b[48];
    struct apb_tunable out[4];
    uint32_t n, s;
    uint32_t rec[6] = {0x80c, 4, 0x1f00, 0, 0x100, 0};
    memcpy(b, rec, 24); memcpy(b + 24, rec, 24);
    assert(apb_parse_tunables(b, 48, APB_ECAM0, out, 4, &n) == APB_OK && n == 2);
    assert(out[0].offset == 0x80c && out[0].mask == 0x1f00 && out[0].value == 0x100);
    assert(apb_parse_tunables(b, 47, APB_ECAM0, out, 4, &n) == APB_PLAN && n == 0);
    assert(apb_parse_tunables(b, 48, APB_ECAM0, out, 1, &n) == APB_PLAN);       /* over cap */
    assert(apb_parse_tunables(b, 48, APB_FUSE, out, 4, &n) == APB_PLAN);        /* read-only */
    assert(apb_parse_tunables(b, 48, APB_PMGR, out, 4, &n) == APB_PLAN);
    uint32_t bad[][6] = {{0x80c, 8, 0x1f00, 0, 0x100, 0},      /* size 8 */
                         {0x80e, 4, 0x1f00, 0, 0x100, 0},      /* unaligned */
                         {0x8000, 4, 0x1, 0, 0x1, 0},          /* outside the 32 KiB slice */
                         {0x80c, 4, 0x1f00, 1, 0x100, 0},      /* 64-bit mask */
                         {0x80c, 4, 0x1f00, 0, 0x200f, 0},     /* value outside mask */
                         {0x80c, 4, 0, 0, 0, 0}};              /* empty mask */
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memcpy(b, bad[i], 24);
        assert(apb_parse_tunables(b, 24, APB_ECAM0, out, 4, &n) == APB_PLAN);
    }
    assert(apb_resolve_link_speed(2, 0, 0, &s) == APB_OK && s == 2);   /* J293 bridge0 */
    assert(apb_resolve_link_speed(1, 3, 0, &s) == APB_OK && s == 3);
    assert(apb_resolve_link_speed(1, 0, 2, &s) == APB_OK && s == 2);
    assert(apb_resolve_link_speed(1, 0, 0, &s) == APB_OK && s == 1);
    assert(apb_resolve_link_speed(7, 0, 0, &s) == APB_OK && s == 4);
    assert(apb_resolve_link_speed(0, 3, 3, &s) == APB_PLAN && s == 0);

    struct apb_record r;
    struct apb_plan p = plan();
    reset_fake();
    p.max_link_speed = 5; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_PLAN && F.reads == 0 && F.write_calls == 0);
    p = plan(); struct apb_tunable wide = {0x4000, 1, 1}; p.common.items = &wide;
    memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_PLAN && F.reads == 0);
    p = plan(); p.rc.count = APB_MAX_TUNABLES + 1;
    memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_PLAN);
    p = plan(); memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, NULL, &r) == APB_ARGUMENT && apb_bringup_port0(NULL, &IO, &r) == APB_ARGUMENT);
}

#ifdef APB_FAKE_REGISTER_FILE
static uint32_t fuse_field(uint32_t raw, unsigned sb, unsigned w) { return (raw >> sb) & ((1u << w) - 1u); }

static void full_sequence(void)
{
    struct apb_record r;
    struct apb_plan p = plan();
    unsigned shared, port;
    int last_shared, first_port;
    reset_fake();
    memset(&r, 0, sizeof r);
    assert(apb_shared_writes_compiled() == 1);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_OK);
    assert(r.result == APB_OK && r.steps_completed == 23 && r.step == APB_STEP_LINK_STATUS);
    assert(r.linksts == 0x1 && r.dirty == 1 && r.gated_writes == 0);
    count_classes(&shared, &port, &last_shared, &first_port);
    assert(shared == r.shared_writes && port == r.port_writes && last_shared < first_port);

    /* PMGR: AUTO and WAS bits cleared, target active; actual follows. */
    for (unsigned off = 0; off < 4; off++) {
        static const uint32_t d[4] = {0x1b8, 0x348, 0x3e8, 0x1a0};
        assert(mem_pmgr[d[off] / 4] == 0x0000000fu);
    }
    assert(F.log[0].region == APB_PMGR && F.log[0].off == 0x1b8);   /* parent IMX first */
    assert(F.log[1].off == 0x348 && F.log[2].off == 0x3e8 && F.log[3].off == 0x1a0);
    /* Shared PHY: both clocks requested, reset released; RC zeroed and PHYIF running. */
    assert(mem_phy[0] == 0x3u && mem_rc[0x4 / 4] == 0 && (mem_rc[0x24 / 4] & 1));
    /* Tunables are RMW: clear mask, OR value. */
    assert(mem_axi[0x10 / 4] == 0x3fu && mem_axi[0x800 / 4] == 0x12u);
    assert(mem_rc[0x54 / 4] == 0x140u && mem_phy[0xc004 / 4] == 0x100u);
    assert(mem_phyip[(0x62c0 - 4) / 4] == 0xdeadbeefu && mem_phyip[0x1d024 / 4] == 1u);
    /* Fuse fields moved into PHY-IP. */
    uint32_t f84 = 0xa5a5a5a5u, f418 = 0x8c3e5d71u, f41c = 0x1bu;
    assert((mem_phyip[0x6238 / 4] & 0x3f) == fuse_field(f84, 4, 6));
    assert(((mem_phyip[0x6220 / 4] >> 14) & 7) == fuse_field(f84, 10, 3));
    assert(((mem_phyip[0x62a4 / 4] >> 17) & 3) == fuse_field(f84, 13, 2));
    uint32_t w = mem_phyip[0x522c / 4];
    assert(((w >> 9) & 3) == fuse_field(f418, 27, 2) && ((w >> 12) & 7) == fuse_field(f418, 13, 3) &&
           ((w >> 16) & 0x1f) == fuse_field(f418, 23, 5));
    assert((w & ~((3u << 9) | (7u << 12) | (0x1fu << 16))) == (0xffffffffu & ~((3u << 9) | (7u << 12) | (0x1fu << 16))));
    assert(((mem_phyip[0x5018 / 4] >> 2) & 1) == fuse_field(f418, 31, 1));
    assert(((mem_phyip[0x1204 / 4] >> 2) & 0x1f) == fuse_field(f41c, 0, 5));
    /* Port 0. */
    assert(mem_port0[0x800 / 4] == 0x100101u && mem_port0[0x814 / 4] == 1u);
    assert(mem_ecam0[0xb84 / 4] == 0x2200u && mem_ecam0[0x8a8 / 4] == 0x3u);
    assert((mem_ecam0[0x7c / 4] & 0xf) == 2 && ((mem_ecam0[0x7c / 4] >> 4) & 0x3f) == 1);
    assert((mem_ecam0[0x9c / 4] & 0x7e) == 0x6);
    assert(mem_ecam0[0xa0 / 4] == 0xabcd0002u);                     /* LNKSTA2 untouched */
    assert(mem_ecam0[0x80c / 4] == ((1u << 17) | (1u << 8)));
    assert(mem_ecam0[0x710 / 4] == (1u << 16));
    assert((mem_ecam0[0x8bc / 4] & 1) == 0);                        /* DBI read-only again */
    /* The only 16-bit write is LNKCTL2. */
    unsigned w16 = 0;
    for (unsigned i = 0; i < F.nw; i++) if (F.log[i].width == 16) { w16++; assert(F.log[i].off == 0xa0); }
    assert(w16 == 1);
    /* No write ever touched the fuse window or anything outside a window. */
    for (unsigned i = 0; i < F.nw; i++) assert(F.log[i].region != APB_FUSE);
    /* A dirty record is refused before any access. */
    unsigned before = F.reads;
    assert(apb_bringup_port0(&p, &IO, &r) == APB_DIRTY && F.reads == before);
}

static void failures_stop_at_their_step(void)
{
    struct apb_record r;
    struct apb_plan p = plan();
    reset_fake(); F.pmgr_stuck = 0x348; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_TIMEOUT && r.step == APB_STEP_POWER);
    assert(F.nw == 2 && r.steps_completed == 0 && r.delay_us_total >= 10000);
    reset_fake(); F.phy_ack_stuck = 4; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_TIMEOUT && r.step == APB_STEP_PHY_CLK0);
    for (unsigned i = 0; i < F.nw; i++) assert(!(F.log[i].region == APB_RC && F.log[i].off == 0x24));
    assert((mem_phy[0] & 0x80) && r.port_writes == 0);              /* reset still asserted */
    reset_fake(); F.phy_ack_stuck = 8; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_TIMEOUT && r.step == APB_STEP_PHY_CLK1);
    reset_fake(); F.run_never = 1; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_TIMEOUT && r.step == APB_STEP_PORT_RUN);
    for (unsigned i = 0; i < F.nw; i++) assert(F.log[i].region != APB_ECAM0);
    assert(r.delay_us_total >= 250000 && r.delay_us_total < 260000);
    reset_fake(); F.busy_forever = 1; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_TIMEOUT && r.step == APB_STEP_PORT_IDLE);
    reset_fake(); F.busy_reads = 3; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_OK && F.linksts_reads == 5);
    reset_fake(); F.fail_read_at = 3; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_IO && r.step == APB_STEP_POWER && F.nw == 1);
    reset_fake(); F.fail_write_at = 5; memset(&r, 0, sizeof r);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_IO && F.nw == 4 && r.dirty == 1);
    reset_fake(); memset(&r, 0, sizeof r);
    struct apb_io no_writer = IO; no_writer.write32 = NULL;
    assert(apb_bringup_port0(&p, &no_writer, &r) == APB_IO && F.write_calls == 0 && r.dirty == 0);
    reset_fake(); memset(&r, 0, sizeof r);
    struct apb_io no16 = IO; no16.write16 = NULL;
    assert(apb_bringup_port0(&p, &no16, &r) == APB_GATED && r.step == APB_STEP_LINK_SPEED);
}
#else
static void gated_build_issues_no_write(void)
{
    struct apb_record r;
    struct apb_plan p = plan();
    reset_fake(); memset(&r, 0, sizeof r);
    assert(apb_shared_writes_compiled() == 0);
    assert(apb_bringup_port0(&p, &IO, &r) == APB_GATED);
    assert(r.step == APB_STEP_POWER && r.steps_completed == 0 && r.gated_writes == 1);
    assert(F.write_calls == 0 && r.shared_writes == 0 && r.port_writes == 0 && r.dirty == 0);
    assert(F.reads == 1 && r.last_region == APB_PMGR && r.last_offset == 0x1b8);
    assert(mem_pmgr[0x1b8 / 4] == 0x10000300u && mem_phy[0] == 0x80u);   /* nothing changed */
}
#endif

int main(void)
{
    parser_plan_and_speed();
#ifdef APB_FAKE_REGISTER_FILE
    full_sequence();
    failures_stop_at_their_step();
    puts("HOST ONLY PASS (fake register file): parser, plan, speed, full 23-step sequence, order, RMW, fuses, 16-bit, failures, dirty");
#else
    gated_build_issues_no_write();
    puts("HOST ONLY PASS (gated, bundle-equivalent): parser, plan, speed, first shared write gated, zero writes");
#endif
    return 0;
}
