/* Host-only tests for linkup.c (slice 3), run after bringup.c (slice 2) against
 * one simulated register file. No hardware.
 * Fake mode:  cc -DAPB_FAKE_REGISTER_FILE ... bringup.c linkup.c test_linkup.c
 * Gated mode: cc ...                       bringup.c linkup.c test_linkup.c  (bundle-equivalent) */
#include "linkup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error test_linkup requires assertions
#endif

/* ---------- simulated register file: slice-2 regions plus the port-0 lane bank ---------- */
static uint32_t mem_pmgr[0x4000 / 4], mem_rc[0x4000 / 4], mem_phy[0x10000 / 4], mem_phyip[0x20000 / 4],
    mem_axi[0x4000 / 4], mem_fuse[0x1000 / 4], mem_port0[0x8000 / 4], mem_ecam0[0x8000 / 4],
    mem_lane[0x4000 / 4];
static uint32_t *const mem[APL_REGIONS] = {mem_pmgr, mem_rc, mem_phy, mem_phyip, mem_axi, mem_fuse,
                                           mem_port0, mem_ecam0, mem_lane};
enum { EV_W32 = 1, EV_W16, EV_PERST, EV_POWER, EV_DELAY };
struct ev { int kind; unsigned region; uint32_t off, val; uint64_t at_us; };
static struct fake {
    /* knobs */
    uint32_t lane_ack_stuck;   /* lane ACK bit (4 or 8) that never latches */
    int ready_never, link_never, link_up_at_start, busy_reads, train_reads;
    uint32_t sid_size, sid_mask;
    int fail_write_at, fail_pin;
    /* state */
    int perst_asserted, perst_ever, power_on;
    uint32_t lane_acks, intstat, linkcmd;
    int training;
    unsigned reads, write_calls, pin_calls, linksts_reads, train_seen, ltssm_writes, nev;
    uint64_t delayed;
    struct ev log[8192];
} F;

static void note(int kind, unsigned region, uint32_t off, uint32_t val)
{
    assert(F.nev < sizeof F.log / sizeof F.log[0]);
    F.log[F.nev++] = (struct ev){kind, region, off, val, F.delayed};
}

static int slice2_run(void)
{
    uint32_t phy = mem_phy[0];
    return (mem_port0[0x800 / 4] & 1) && (mem_port0[0x814 / 4] & 1) && (mem_rc[0x24 / 4] & 1) &&
           !(phy & 0x80) && (phy & 3) == 3;
}
static int lane_clock_ok(void)
{
    return (mem_lane[0] & 0x600) == 0x600 && (F.lane_acks & 0xc) == 0xc && (mem_port0[0x810 / 4] & 1);
}
static int port_ready(void)
{
    if (F.ready_never || F.perst_asserted || !slice2_run()) return 0;
    return F.perst_ever ? lane_clock_ok() : 1;
}
static int sid_index(unsigned r, uint32_t off, uint32_t *idx)
{
    if (r != APB_PORT0 || off < 0x828 || off >= 0x828 + 4 * 64) return 0;
    *idx = (off - 0x828) / 4;
    return 1;
}

static int fake_read32(void *ctx, unsigned r, uint32_t off, uint32_t *out)
{
    uint32_t v, idx;
    (void)ctx;
    assert(r < APL_REGIONS && !(off & 3) && off < apl_region_size(r));
    F.reads++;
    v = mem[r][off / 4];
    if (r == APB_PMGR && (off == 0x1b8 || off == 0x348 || off == 0x3e8 || off == 0x1a0)) {
        v = (v & ~0xf0u) | ((v & 0xf) << 4);
    } else if (r == APB_PHY && off == 0) {
        v &= ~0xcu;
        if (v & 1) v |= 4;
        if (v & 2) v |= 8;
    } else if (r == APL_P0PHY && off == 0) {
        v = (v & ~0xcu) | F.lane_acks;
    } else if (r == APB_PORT0 && off == 0x804) {
        v = (v & ~1u) | (port_ready() ? 1u : 0u);
    } else if (r == APB_PORT0 && off == 0x208) {
        F.linksts_reads++;
        if (F.link_up_at_start) v = 0x1;
        else if (!F.training) v = (int)F.linksts_reads <= F.busy_reads ? 0x4u : 0x0u;   /* busy, then idle */
        else if (!F.link_never && port_ready() && (int)++F.train_seen > F.train_reads) {
            v = 0x1; F.intstat |= 1u << 12;                                              /* link up */
        } else v = 0x4u;
    } else if (r == APB_PORT0 && off == 0x100) {
        v = F.intstat;
    } else if (r == APB_PORT0 && off == 0x210) {
        v = F.linkcmd;
    } else if (sid_index(r, off, &idx)) {
        v = idx < F.sid_size ? mem_port0[off / 4] : 0;                                   /* RAZ beyond table */
    }
    *out = v;
    return 0;
}

static int fake_write32(void *ctx, unsigned r, uint32_t off, uint32_t val)
{
    uint32_t idx;
    (void)ctx;
    assert(r < APL_REGIONS && r != APB_FUSE && !(off & 3) && off < apl_region_size(r));
    F.write_calls++;
    if (F.fail_write_at && (int)F.write_calls == F.fail_write_at) return -1;
    note(EV_W32, r, off, val);
    if (r == APB_PMGR) val &= ~0xf0u;
    if (r == APB_PHY && off == 0) val &= ~0xcu;
    if (r == APL_P0PHY && off == 0) {
        val &= ~0xcu;
        if (mem_lane[1] & (1u << 15)) {                   /* requests honoured only with config access open */
            if ((val & 1) && F.lane_ack_stuck != 4) F.lane_acks |= 4;
            if ((val & 2) && F.lane_ack_stuck != 8) F.lane_acks |= 8;
        }
    }
    if (r == APB_PORT0 && off == 0x100) { F.intstat &= ~val; return 0; }          /* write-one-to-clear */
    if (r == APB_PORT0 && off == 0x210) { F.linkcmd &= ~val; return 0; }
    if (r == APB_PORT0 && off == 0x80) { F.ltssm_writes++; if (val & 1) F.training = 1; return 0; }
    if (sid_index(r, off, &idx)) {
        if (idx < F.sid_size) mem_port0[off / 4] = val & F.sid_mask;                   /* WI beyond table */
        return 0;
    }
    mem[r][off / 4] = val;
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
    note(EV_W16, r, off, val);
    mem_ecam0[off / 4] = (mem_ecam0[off / 4] & 0xffff0000u) | val;
    return 0;
}
static void fake_delay(void *ctx, uint32_t us) { (void)ctx; F.delayed += us; if (us >= 100) note(EV_DELAY, 0, 0, us); }
static int fake_perst(void *ctx, int asserted)
{
    (void)ctx;
    F.pin_calls++;
    if (F.fail_pin) return -1;
    note(EV_PERST, 0, 0, (uint32_t)asserted);
    F.perst_asserted = asserted;
    if (asserted) F.perst_ever = 1;
    return 0;
}
static int fake_power(void *ctx, int on)
{
    (void)ctx;
    F.pin_calls++;
    note(EV_POWER, 0, 0, (uint32_t)on);
    F.power_on = on;
    return 0;
}

static const struct apb_io IO = {NULL, fake_read32, fake_write32, fake_read16, fake_write16, fake_delay};
static const struct apl_pins PINS = {NULL, fake_perst, fake_power};

/* ---------- slice-2 plan (synthetic tunables) and slice-3 plan (J293 ADT values) ---------- */
static const struct apb_tunable T_AXI[1] = {{0x800, 0xff, 0x12}};
static const struct apb_tunable T_COMMON[1] = {{0x54, 0xffff, 0x140}};
static const struct apb_tunable T_PHY[1] = {{0x8, 0x3, 0x1}};
static const struct apb_tunable T_PLL[1] = {{0x62bc, 0xffffffffu, 0xdeadbeefu}};
static const struct apb_tunable T_AUSPMA[1] = {{0x1d024, 0x1, 0x1}};
static const struct apb_tunable T_CONFIG[1] = {{0x800, 0x100100, 0x100100}};   /* sets APPCLK no-gate bit 8 */
static const struct apb_tunable T_RC[1] = {{0xb84, 0xff00, 0x2200}};
static const struct apb_tunable T_GEN[1] = {{0x8a8, 0x1, 0x1}};
static struct apb_plan bplan(void)
{
    struct apb_plan p = {{T_AXI, 1}, {T_COMMON, 1}, {T_PHY, 1}, {T_PLL, 1}, {T_AUSPMA, 1},
                         {T_CONFIG, 1}, {T_RC, 1}, {T_GEN, 1}, {T_GEN, 1}, 2};
    return p;
}
static struct apl_plan lplan(void)
{
    struct apl_plan p;
    memset(&p, 0, sizeof p);
    p.refclk_to_perst_us = 100;      /* ADT t-refclk-to-perst */
    p.perst_to_config_ms = 100;      /* ADT perst-to-config */
    p.msi_doorbell = 0xfffff000u;    /* ADT apcie msi-address */
    p.msi_vectors = 8;               /* ADT pci-bridge0 #msi-vectors */
    p.link_timeout_ms = 500;
    p.sid_count = 2;
    p.sids[0].rid = 0x100; p.sids[0].sid = 1;
    p.sids[1].rid = 0x101; p.sids[1].sid = 2;
    return p;
}

static void reset_fake(void)
{
    for (unsigned r = 0; r < APL_REGIONS; r++) memset(mem[r], 0, apl_region_size(r));
    memset(&F, 0, sizeof F);
    mem_pmgr[0x1b8 / 4] = mem_pmgr[0x348 / 4] = mem_pmgr[0x3e8 / 4] = mem_pmgr[0x1a0 / 4] = 0x10000300u;
    mem_phy[0] = 0x80u;
    mem_ecam0[0xa0 / 4] = 0xabcd0000u;
    mem_port0[0x810 / 4] = 1u << 8;   /* refclk gating disabled, clock off */
    F.intstat = 0x8000u | 0x4000u;    /* stale AF-timeout and link-down status */
    F.linkcmd = 0x3u;
    F.busy_reads = 2;
    F.train_reads = 5;
    F.sid_size = 16;
    F.sid_mask = 0x800fffffu;         /* valid, 4-bit stream, 16-bit requester */
}

static void plan_checks(void)
{
    struct apl_plan p = lplan();
    assert(apl_plan_ok(&p));
    p = lplan(); p.refclk_to_perst_us = 99; assert(!apl_plan_ok(&p));
    p = lplan(); p.perst_to_config_ms = 99; assert(!apl_plan_ok(&p));
    p = lplan(); p.perst_to_config_ms = 1001; assert(!apl_plan_ok(&p));
    p = lplan(); p.power_enable = 1; p.power_on_ms = 50; assert(!apl_plan_ok(&p));
    p = lplan(); p.power_enable = 1; p.power_on_ms = 100; assert(apl_plan_ok(&p));
    p = lplan(); p.power_enable = 2; assert(!apl_plan_ok(&p));
    p = lplan(); p.msi_vectors = 6; assert(!apl_plan_ok(&p));
    p = lplan(); p.msi_vectors = 64; assert(!apl_plan_ok(&p));
    p = lplan(); p.msi_doorbell = 0xfffff002u; assert(!apl_plan_ok(&p));
    p = lplan(); p.msi_doorbell = 0; assert(!apl_plan_ok(&p));
    p = lplan(); p.msi_vectors = 0; p.msi_doorbell = 0; assert(apl_plan_ok(&p));
    p = lplan(); p.link_timeout_ms = 0; assert(!apl_plan_ok(&p));
    p = lplan(); p.link_timeout_ms = 1001; assert(!apl_plan_ok(&p));
    p = lplan(); p.sid_count = APL_MAX_SIDS + 1; assert(!apl_plan_ok(&p));
    p = lplan(); p.sids[1].rid = 0x100; assert(!apl_plan_ok(&p));          /* duplicate requester */
    p = lplan(); p.sids[0].rid = 0x0008; assert(!apl_plan_ok(&p));         /* bus 0 is the root port */
    p = lplan(); p.sids[0].sid = 0x10; assert(!apl_plan_ok(&p));           /* wider than the probed field */
    assert(!apl_plan_ok(NULL));
    assert(apl_region_shared(APL_P0PHY) && apl_region_shared(APB_PHY) && !apl_region_shared(APB_PORT0));
    assert(apl_region_size(APL_P0PHY) == 0x4000 && apl_region_size(APL_REGIONS) == 0);

    /* Refusals before any access. */
    struct apl_record r;
    struct apb_record b;
    reset_fake();
    memset(&b, 0, sizeof b);
    memset(&r, 0, sizeof r);
    p = lplan(); p.msi_vectors = 3;
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_PLAN && F.reads == 0);
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, NULL, &b, &r) == APB_ARGUMENT && F.reads == 0);
    assert(apl_link_port0(NULL, &IO, &PINS, &b, &r) == APB_ARGUMENT);
    assert(apl_link_port0(&p, &IO, &PINS, &b, NULL) == APB_ARGUMENT);
    p = lplan(); memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE);            /* empty slice-2 record */
    assert(r.step == APL_STEP_PRECONDITION && F.reads == 0 && F.write_calls == 0 && F.pin_calls == 0);
    assert(apl_link_port0(&p, &IO, &PINS, NULL, &r) == APL_STATE && F.reads == 0);
}

#ifdef APB_FAKE_REGISTER_FILE
static int find(int kind, unsigned region, uint32_t off, uint32_t val, unsigned from)
{
    for (unsigned i = from; i < F.nev; i++)
        if (F.log[i].kind == kind && F.log[i].region == region && F.log[i].off == off && F.log[i].val == val)
            return (int)i;
    return -1;
}
static int find_pin(int kind, uint32_t level, unsigned from)
{
    for (unsigned i = from; i < F.nev; i++) if (F.log[i].kind == kind && F.log[i].val == level) return (int)i;
    return -1;
}

static void run_slice2(struct apb_record *b)
{
    struct apb_plan bp = bplan();
    memset(b, 0, sizeof *b);
    assert(apb_bringup_port0(&bp, &IO, b) == APB_OK && b->steps_completed == 23);
    assert(b->linksts == 0 && !F.training);   /* m1n1's sequence never starts link training */
}

static void full_link(void)
{
    struct apb_record b;
    struct apl_record r;
    struct apl_plan p = lplan();
    reset_fake();
    run_slice2(&b);
    unsigned s3 = F.nev;                           /* first slice-3 event */
    uint64_t t0 = F.delayed;
    memset(&r, 0, sizeof r);
    assert(apl_shared_writes_compiled() == 1);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_OK);
    assert(r.result == APB_OK && r.steps_completed == 21 && r.step == APL_STEP_LINK_STATUS);
    assert(r.linksts == 0x1 && (r.intstat & (1u << 12)) && !(r.intstat & 0xc000u));
    assert(r.gated_writes == 0 && r.dirty == 1 && r.pin_calls == 2 && r.rid2sid_entries == 16);
    assert(r.shared_writes == 5);                  /* the lane bank: open, req0, req1, close, enable */

    /* Every slice-3 event is a pin call, a delay, the lane bank or the port block. */
    for (unsigned i = s3; i < F.nev; i++)
        assert(F.log[i].kind != EV_W32 || F.log[i].region == APB_PORT0 || F.log[i].region == APL_P0PHY);
    assert(F.log[s3].kind == EV_PERST && F.log[s3].val == 1);              /* PERST# first */

    /* Lane reference clock handshake, in order, with config access open. */
    int open = find(EV_W32, APL_P0PHY, 4, 1u << 15, s3);
    int q0 = find(EV_W32, APL_P0PHY, 0, 0x1, s3);
    /* Read-modify-write carries the read-only ACK bits back, as set32-style RMW does;
     * the register ignores them, so the stored value below has none. */
    int q1 = find(EV_W32, APL_P0PHY, 0, 0x7, s3);
    int shut = find(EV_W32, APL_P0PHY, 4, 0, s3);
    int en = find(EV_W32, APL_P0PHY, 0, 0x60f, s3);
    int refclk = find(EV_W32, APB_PORT0, 0x810, 0x101, s3);
    assert(open > (int)s3 && q0 > open && q1 > q0 && shut > q1 && en > shut && refclk > en);
    assert(mem_lane[0] == 0x603 && mem_lane[1] == 0 && F.lane_acks == 0xc);

    /* refclk -> PERST: delay, then port PERST off, then the pin released, then config delay. */
    int d1 = find(EV_DELAY, 0, 0, 100, (unsigned)refclk);
    int perst = find(EV_W32, APB_PORT0, 0x814, 0x1, (unsigned)refclk);
    int release = find_pin(EV_PERST, 0, (unsigned)refclk);
    int d2 = find(EV_DELAY, 0, 0, 100000, (unsigned)release);
    assert(d1 > refclk && perst > d1 && release > perst && d2 > release);
    assert(!F.perst_asserted && F.power_on == 0);

    /* Clock gating allowed; interrupts masked and cleared; MSI; RID2SID; LTSSM last. */
    assert(mem_port0[0x810 / 4] == 0x1 && mem_port0[0x800 / 4] == 0x100001u);
    assert(mem_port0[0x104 / 4] == 0xffffffffu && F.linkcmd == 0);
    int mask = find(EV_W32, APB_PORT0, 0x104, 0xffffffffu, (unsigned)d2);
    assert(mask > d2 && find(EV_W32, APB_PORT0, 0x100, 0xffffffffu, (unsigned)mask) > mask);
    assert(mem_port0[0x168 / 4] == 0xfffff000u && mem_port0[0x128 / 4] == 0 && mem_port0[0x124 / 4] == 0x31u);
    assert(mem_port0[0x828 / 4] == 0x80010100u && mem_port0[0x82c / 4] == 0x80020101u);
    for (unsigned i = 2; i < 16; i++) assert(mem_port0[(0x828 + 4 * i) / 4] == 0);
    int ltssm = find(EV_W32, APB_PORT0, 0x80, 0x1, s3);
    int map = find(EV_W32, APB_PORT0, 0x82c, 0x80020101u, s3);
    assert(F.ltssm_writes == 1 && ltssm > map && map > mask);
    for (unsigned i = (unsigned)ltssm + 1; i < F.nev; i++) assert(F.log[i].kind != EV_W32);
    assert(F.delayed - t0 >= 100 + 100000 && r.delay_us_total == F.delayed - t0);

    /* A dirty record is refused before any access. */
    unsigned reads = F.reads;
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_DIRTY && F.reads == reads);
}

static void msi_and_power_variants(void)
{
    struct apb_record b;
    struct apl_record r;
    struct apl_plan p = lplan();
    reset_fake(); run_slice2(&b);
    p.msi_vectors = 32;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_OK && mem_port0[0x124 / 4] == 0x51u);

    reset_fake(); run_slice2(&b);
    p = lplan(); p.msi_vectors = 0; p.msi_doorbell = 0;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_OK);
    assert(mem_port0[0x124 / 4] == 0 && mem_port0[0x168 / 4] == 0);   /* MSI left unconfigured */

    reset_fake(); run_slice2(&b);
    unsigned s3 = F.nev;
    p = lplan(); p.power_enable = 1; p.power_on_ms = 120;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_OK && F.power_on == 1 && r.pin_calls == 3);
    int on = find_pin(EV_POWER, 1, s3);
    assert(on == (int)s3 + 1 && find(EV_DELAY, 0, 0, 120000, (unsigned)on) > on);

    reset_fake(); run_slice2(&b);
    p = lplan(); p.sid_count = 0;
    F.sid_size = 0;                                  /* table reads as zero, ignores writes */
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_OK && r.rid2sid_entries == 0);

    reset_fake(); run_slice2(&b);
    p = lplan(); F.sid_size = 64;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_OK && r.rid2sid_entries == 64);
}

static void preconditions_and_failures(void)
{
    struct apb_record b;
    struct apl_record r;
    struct apl_plan p = lplan();
    unsigned w;

    /* Link already up: refused after one read, no write or pin call. */
    reset_fake(); run_slice2(&b); F.link_up_at_start = 1; w = F.write_calls;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE && r.step == APL_STEP_PRECONDITION);
    assert(r.reads == 1 && F.write_calls == w && F.pin_calls == 0 && r.dirty == 0);
    /* APPCLK off: refused. */
    reset_fake(); run_slice2(&b); mem_port0[0x800 / 4] &= ~1u; w = F.write_calls;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE && r.reads == 2 && F.write_calls == w);
    /* A slice-2 record with a gated write, or one not finished, is refused before any access. */
    reset_fake(); run_slice2(&b); b.gated_writes = 1;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE && r.reads == 0);
    reset_fake(); run_slice2(&b); b.steps_completed = 22;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE && r.reads == 0);

    /* Lane ACK 0 never comes: stop there, device still held in PERST#, no port PERST/LTSSM write. */
    reset_fake(); run_slice2(&b); F.lane_ack_stuck = 4;
    unsigned s3 = F.nev;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_TIMEOUT && r.step == APL_STEP_LANE_REFCLK0);
    assert(F.perst_asserted && F.ltssm_writes == 0 && find(EV_W32, APB_PORT0, 0x814, 1, s3) < 0);
    assert(r.delay_us_total >= 50000 && r.delay_us_total < 50200);
    reset_fake(); run_slice2(&b); F.lane_ack_stuck = 8;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_TIMEOUT && r.step == APL_STEP_LANE_REFCLK1);
    /* Port never ready. */
    reset_fake(); run_slice2(&b); F.ready_never = 1;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_TIMEOUT && r.step == APL_STEP_PORT_READY);
    assert(F.ltssm_writes == 0 && mem_port0[0x104 / 4] == 0);
    /* Link never up: the bounded wait uses the plan timeout. */
    reset_fake(); run_slice2(&b); F.link_never = 1;
    memset(&r, 0, sizeof r);
    uint64_t before = r.delay_us_total;
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_TIMEOUT && r.step == APL_STEP_LINK_UP);
    assert(F.ltssm_writes == 1 && r.delay_us_total - before >= 100 + 100000 + 500000);
    /* RID2SID: table too small for the plan; narrow stream field fails the probe. */
    reset_fake(); run_slice2(&b); F.sid_size = 1;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE && r.step == APL_STEP_RID2SID_MAP);
    assert(r.rid2sid_entries == 1 && F.ltssm_writes == 0);
    reset_fake(); run_slice2(&b); F.sid_mask = 0x8007ffffu;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE && r.rid2sid_entries == 0);
    assert(mem_port0[0x828 / 4] == 0);               /* the partial probe value was cleared */
    /* Pin and write faults. */
    reset_fake(); run_slice2(&b); F.fail_pin = 1;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_IO && r.step == APL_STEP_PERST_ASSERT);
    reset_fake(); run_slice2(&b); F.fail_write_at = (int)F.write_calls + 3;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_IO && r.dirty == 1);
    reset_fake(); run_slice2(&b);
    struct apl_pins no_perst = PINS; no_perst.perst = NULL;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &no_perst, &b, &r) == APB_IO && r.dirty == 0);
    reset_fake(); run_slice2(&b);
    struct apb_io no_writer = IO; no_writer.write32 = NULL;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &no_writer, &PINS, &b, &r) == APB_IO && r.step == APL_STEP_LANE_CONFIG_OPEN);
}
#else
static void gated_build_issues_no_write(void)
{
    struct apb_record b;
    struct apl_record r;
    struct apb_plan bp = bplan();
    struct apl_plan p = lplan();
    reset_fake();
    assert(apl_shared_writes_compiled() == 0);
    /* A real gated slice-2 run cannot produce the record slice 3 needs. */
    memset(&b, 0, sizeof b);
    assert(apb_bringup_port0(&bp, &IO, &b) == APB_GATED);
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APL_STATE && F.reads == 1 && F.write_calls == 0);
    /* Even with a forged complete record, the first write (PERST#) is gated. */
    reset_fake();
    memset(&b, 0, sizeof b);
    b.version = 1; b.result = APB_OK; b.steps_completed = 23; b.shared_writes = 100; b.dirty = 1;
    mem_port0[0x800 / 4] = 1;
    memset(&r, 0, sizeof r);
    assert(apl_link_port0(&p, &IO, &PINS, &b, &r) == APB_GATED);
    assert(r.step == APL_STEP_PERST_ASSERT && r.steps_completed == 1 && r.gated_writes == 1);
    assert(r.reads == 2 && F.write_calls == 0 && F.pin_calls == 0 && r.dirty == 0);
    assert(r.shared_writes == 0 && r.port_writes == 0 && F.nev == 0);
}
#endif

int main(void)
{
    plan_checks();
#ifdef APB_FAKE_REGISTER_FILE
    full_link();
    msi_and_power_variants();
    preconditions_and_failures();
    puts("HOST ONLY PASS (fake register file): plan, preconditions, slice 2 then 21-step link stage, order, lane handshake, PERST timing, clock gating, irq quiesce, MSI, RID2SID, LTSSM, failures, dirty");
#else
    gated_build_issues_no_write();
    puts("HOST ONLY PASS (gated, bundle-equivalent): plan, preconditions, gated slice-2 record refused, forged record stops at PERST# with zero writes");
#endif
    return 0;
}
