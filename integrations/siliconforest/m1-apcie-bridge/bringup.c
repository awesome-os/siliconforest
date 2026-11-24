/* Original darwin-lab port-0 bring-up engine; see bringup.h and NOTICE-bringup. */
#include "bringup.h"
#include <string.h>

#if defined(KERNEL) && defined(APB_FAKE_REGISTER_FILE)
#error "APB_FAKE_REGISTER_FILE is host-test only: a kernel build must not contain shared writes"
#endif

#define APB_MAX_READS   100000u
#define APB_MAX_WRITES  4096u
#define POLL_STEP_US    10u

/* PMGR power-state register (m1n1 pmgr.c) */
#define PMGR_AUTO_ENABLE   (1u << 28)
#define PMGR_WAS_CLKGATED  (1u << 9)
#define PMGR_WAS_PWRGATED  (1u << 8)
#define PMGR_PS_ACTUAL     0xf0u
#define PMGR_PS_TARGET     0x0fu
#define PMGR_PS_ACTIVE     0xfu
#define PMGR_TIMEOUT_US    10000u
/* Parent-first order of /arm-io/apcie clock-gates [77,97,29]: IMX(32) is the
 * parent of APCIE(77), which is the parent of APCIE_GP(97); PCIE_REF(29) has no
 * parent. Each domain is enabled once (m1n1 re-enables shared parents). */
static const uint32_t pmgr_domains[4] = {0x1b8, 0x348, 0x3e8, 0x1a0};

/* Shared PHY and RC (m1n1 pcie.c) */
#define PHY_CTRL           0x000u
#define PHY_CLK0REQ        (1u << 0)
#define PHY_CLK1REQ        (1u << 1)
#define PHY_CLK0ACK        (1u << 2)
#define PHY_CLK1ACK        (1u << 3)
#define PHY_RESET_T8103    (1u << 7)
#define PHY_CLK_TIMEOUT_US 50000u
#define RC_ZERO            0x004u
#define RC_PHYIF_CTRL      0x024u
#define RC_PHYIF_RUN       (1u << 0)
/* Port 0 */
#define PORT_LINKSTS       0x208u
#define PORT_LINKSTS_BUSY  (1u << 2)
#define PORT_APPCLK        0x800u
#define PORT_APPCLK_EN     (1u << 0)
#define PORT_STATUS        0x804u
#define PORT_STATUS_RUN    (1u << 0)
#define PORT_RESET         0x814u
#define PORT_RESET_DIS     (1u << 0)
#define PORT_TIMEOUT_US    250000u
/* DesignWare DBI and PCIe capability in the port 0 ECAM slice */
#define DBI_RO_WR          0x8bcu
#define DBI_RO_WR_EN       (1u << 0)
#define DBI_PORT_LINK_CTRL 0x710u
#define DBI_LINK_MODE      (0x3fu << 16)
#define DBI_LINK_MODE_1    (0x1u << 16)
#define DBI_WIDTH_SPEED    0x80cu
#define DBI_LINK_WIDTH     (0x1fu << 8)
#define DBI_SPEED_CHANGE   (1u << 17)
#define CAP_LNKCAP         0x7cu    /* 0x70 + 0x0c */
#define LNKCAP_SLS         0xfu
#define LNKCAP_MLW         (0x3fu << 4)
#define CAP_LNKCAP2        0x9cu    /* 0x70 + 0x2c */
#define LNKCAP2_SLS        (0x3fu << 1)
#define CAP_LNKCTL2        0xa0u    /* 0x70 + 0x30, 16-bit */
#define LNKCTL2_TLS        0xfu

/* T8103 fuse transfer: (fuse offset, PHY-IP offset, source bit, target bit,
 * width). Data from m1n1 pcie.c pcie_fuse_bits_t8103 (MIT). */
struct fuse_bits { uint16_t src, tgt; uint8_t src_bit, tgt_bit, width; };
static const struct fuse_bits fuses_t8103[11] = {
    {0x0084, 0x6238, 4, 0, 6},   {0x0084, 0x6220, 10, 14, 3}, {0x0084, 0x62a4, 13, 17, 2},
    {0x0418, 0x522c, 27, 9, 2},  {0x0418, 0x522c, 13, 12, 3}, {0x0418, 0x5220, 18, 14, 3},
    {0x0418, 0x52a4, 21, 17, 2}, {0x0418, 0x522c, 23, 16, 5}, {0x0418, 0x5278, 23, 20, 3},
    {0x0418, 0x5018, 31, 2, 1},  {0x041c, 0x1204, 0, 2, 5},
};

static const uint32_t region_sizes[APB_REGIONS] = {
    0x4000, 0x4000, 0x10000, 0x20000, 0x4000, 0x1000, 0x8000, 0x8000
};

uint32_t apb_region_size(unsigned region) { return region < APB_REGIONS ? region_sizes[region] : 0; }
int apb_region_shared(unsigned region) { return region <= APB_FUSE; }
int apb_shared_writes_compiled(void)
{
#ifdef APB_FAKE_REGISTER_FILE
    return 1;
#else
    return 0;
#endif
}

/* Fixed (non-tunable) write targets. Tunable targets are validated by the parser. */
static int fixed_write_ok(unsigned region, uint32_t off)
{
    unsigned i;
    switch (region) {
    case APB_PMGR:
        for (i = 0; i < 4; i++) if (off == pmgr_domains[i]) return 1;
        return 0;
    case APB_RC:    return off == RC_ZERO || off == RC_PHYIF_CTRL;
    case APB_PHY:   return off == PHY_CTRL;
    case APB_PHYIP:
        for (i = 0; i < 11; i++) if (off == fuses_t8103[i].tgt) return 1;
        return 0;
    case APB_PORT0: return off == PORT_APPCLK || off == PORT_RESET;
    case APB_ECAM0:
        return off == DBI_RO_WR || off == DBI_PORT_LINK_CTRL || off == DBI_WIDTH_SPEED ||
               off == CAP_LNKCAP || off == CAP_LNKCAP2;
    default:        return 0;
    }
}
static int fuse_read_ok(uint32_t off) { return off == 0x84 || off == 0x418 || off == 0x41c; }

struct ctx { const struct apb_io *io; struct apb_record *rec; };

static int in_window(unsigned region, uint32_t off, uint32_t width)
{
    return region < APB_REGIONS && !(off & (width - 1)) && off <= region_sizes[region] - width;
}

static int rd(struct ctx *c, unsigned region, uint32_t off, uint32_t *v)
{
    struct apb_record *r = c->rec;
    if (!in_window(region, off, 4) || (region == APB_FUSE && !fuse_read_ok(off))) return APB_ARGUMENT;
    r->last_region = region; r->last_offset = off;
    if (r->reads >= APB_MAX_READS) return APB_LIMIT;
    r->reads++;
    if (c->io->read32(c->io->context, region, off, v)) return APB_IO;
    r->last_value = *v;
    return APB_OK;
}

/* The single write path. tunable=1 means off came from a validated tunable. */
static int wr(struct ctx *c, unsigned region, uint32_t off, uint32_t v, int tunable)
{
    struct apb_record *r = c->rec;
    if (!in_window(region, off, 4) || region == APB_FUSE) return APB_ARGUMENT;
    if (!tunable && !fixed_write_ok(region, off)) return APB_ARGUMENT;
    r->last_region = region; r->last_offset = off; r->last_value = v;
    if (r->shared_writes + r->port_writes + r->gated_writes >= APB_MAX_WRITES) return APB_LIMIT;
    if (apb_region_shared(region)) {
#ifdef APB_FAKE_REGISTER_FILE
        if (!c->io->write32) return APB_IO;
        r->dirty = 1;
        if (c->io->write32(c->io->context, region, off, v)) return APB_IO;
        r->shared_writes++;
        return APB_OK;
#else
        r->gated_writes++;           /* no writer is referenced on this path */
        return APB_GATED;
#endif
    }
    if (!c->io->write32) { r->gated_writes++; return APB_GATED; }
    r->dirty = 1;
    if (c->io->write32(c->io->context, region, off, v)) return APB_IO;
    r->port_writes++;
    return APB_OK;
}

static int mask32(struct ctx *c, unsigned region, uint32_t off, uint32_t clear, uint32_t set, int tunable)
{
    uint32_t v;
    int e = rd(c, region, off, &v);
    if (e) return e;
    return wr(c, region, off, (v & ~clear) | set, tunable);
}

/* 16-bit RMW, only for the port 0 Link Control 2 register (its upper half is
 * Link Status 2, which must not be written back). */
static int mask16_lnkctl2(struct ctx *c, uint16_t clear, uint16_t set)
{
    struct apb_record *r = c->rec;
    uint16_t v;
    if (!c->io->read16) return APB_IO;
    r->last_region = APB_ECAM0; r->last_offset = CAP_LNKCTL2;
    if (r->reads >= APB_MAX_READS) return APB_LIMIT;
    r->reads++;
    if (c->io->read16(c->io->context, APB_ECAM0, CAP_LNKCTL2, &v)) return APB_IO;
    if (r->shared_writes + r->port_writes + r->gated_writes >= APB_MAX_WRITES) return APB_LIMIT;
    if (!c->io->write16) { r->gated_writes++; return APB_GATED; }
    v = (uint16_t)((v & ~clear) | set);
    r->last_value = v; r->dirty = 1;
    if (c->io->write16(c->io->context, APB_ECAM0, CAP_LNKCTL2, v)) return APB_IO;
    r->port_writes++;
    return APB_OK;
}

static void delay(struct ctx *c, uint32_t us)
{
    if (c->io->delay_us) c->io->delay_us(c->io->context, us);
    c->rec->delay_us_total += us;
}

static int poll(struct ctx *c, unsigned region, uint32_t off, uint32_t mask, uint32_t want,
                uint32_t timeout_us)
{
    uint32_t waited = 0, v;
    c->rec->polls++;
    for (;;) {
        int e = rd(c, region, off, &v);
        if (e) return e;
        if ((v & mask) == want) return APB_OK;
        if (waited >= timeout_us) return APB_TIMEOUT;
        delay(c, POLL_STEP_US);
        waited += POLL_STEP_US;
    }
}

static int tunable_ok(unsigned region, const struct apb_tunable *t)
{
    return in_window(region, t->offset, 4) && !(t->value & ~t->mask) && t->mask;
}
static int set_ok(unsigned region, const struct apb_tunables *s)
{
    uint32_t i;
    if (s->count > APB_MAX_TUNABLES || (s->count && !s->items)) return 0;
    for (i = 0; i < s->count; i++) if (!tunable_ok(region, &s->items[i])) return 0;
    return 1;
}
static int apply(struct ctx *c, unsigned region, const struct apb_tunables *s)
{
    uint32_t i;
    for (i = 0; i < s->count; i++) {
        const struct apb_tunable *t = &s->items[i];
        int e = mask32(c, region, t->offset, t->mask, t->value, 1);
        if (e) return e;
    }
    return APB_OK;
}

int apb_parse_tunables(const uint8_t *b, size_t n, unsigned region, struct apb_tunable *out,
                       uint32_t cap, uint32_t *count)
{
    size_t i;
    uint32_t k = 0;
    if (count) *count = 0;
    if (!b || !out || !count || region >= APB_REGIONS || region == APB_FUSE || region == APB_PMGR ||
        !n || n % 24 || n / 24 > cap || n / 24 > APB_MAX_TUNABLES)
        return APB_PLAN;
    for (i = 0; i < n; i += 24) {
        uint32_t off, size, mlo, mhi, vlo, vhi;
        memcpy(&off, b + i, 4); memcpy(&size, b + i + 4, 4);
        memcpy(&mlo, b + i + 8, 4); memcpy(&mhi, b + i + 12, 4);
        memcpy(&vlo, b + i + 16, 4); memcpy(&vhi, b + i + 20, 4);
        if (size != 4 || mhi || vhi) return APB_PLAN;
        out[k].offset = off; out[k].mask = mlo; out[k].value = vlo;
        if (!tunable_ok(region, &out[k])) return APB_PLAN;
        k++;
    }
    *count = k;
    return APB_OK;
}

int apb_resolve_link_speed(uint32_t bridge_max, uint32_t child_target, uint32_t child_expected,
                           uint32_t *out)
{
    uint32_t s = bridge_max;
    if (!out) return APB_ARGUMENT;
    *out = 0;
    if (s == 1) {
        if (child_target) s = child_target;
        else if (child_expected) s = child_expected;
    }
    if (!s) return APB_PLAN;
    *out = s > 4 ? 4 : s;
    return APB_OK;
}

static int pmgr_enable(struct ctx *c, uint32_t off)
{
    int e = mask32(c, APB_PMGR, off,
                   PMGR_AUTO_ENABLE | PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED | PMGR_PS_TARGET,
                   PMGR_PS_ACTIVE, 0);
    if (e) return e;
    return poll(c, APB_PMGR, off, PMGR_PS_ACTUAL, PMGR_PS_ACTIVE << 4, PMGR_TIMEOUT_US);
}

static int fuses(struct ctx *c)
{
    unsigned i;
    for (i = 0; i < 11; i++) {
        const struct fuse_bits *f = &fuses_t8103[i];
        uint32_t raw, field, m = (1u << f->width) - 1u;
        int e = rd(c, APB_FUSE, f->src, &raw);
        if (e) return e;
        field = (raw >> f->src_bit) & m;
        e = mask32(c, APB_PHYIP, f->tgt, m << f->tgt_bit, field << f->tgt_bit, 0);
        if (e) return e;
    }
    return APB_OK;
}

#define STEP(id) do { r->step = (id); } while (0)
#define CHECK(expr) do { e = (expr); if (e) goto done; } while (0)
#define DONE() (r->steps_completed++)

int apb_bringup_port0(const struct apb_plan *p, const struct apb_io *io, struct apb_record *r)
{
    struct ctx c;
    unsigned i;
    int e;
    uint32_t speed;

    if (!r) return APB_ARGUMENT;
    if (r->dirty) { r->result = APB_DIRTY; return APB_DIRTY; }
    memset(r, 0, sizeof(*r));
    r->version = 1;
    r->result = APB_ARGUMENT;
    r->last_region = APB_REGIONS;
    if (!p || !io || !io->read32) return APB_ARGUMENT;
    speed = p->max_link_speed;
    if (!set_ok(APB_AXI, &p->axi2af) || !set_ok(APB_RC, &p->common) || !set_ok(APB_PHY, &p->phy) ||
        !set_ok(APB_PHYIP, &p->phy_ip_pll) || !set_ok(APB_PHYIP, &p->phy_ip_auspma) ||
        !set_ok(APB_PORT0, &p->port_config) || !set_ok(APB_ECAM0, &p->rc) ||
        !set_ok(APB_ECAM0, &p->rc_gen3) || !set_ok(APB_ECAM0, &p->rc_gen4) || speed < 1 || speed > 4) {
        r->result = APB_PLAN;
        return APB_PLAN;
    }
    c.io = io;
    c.rec = r;

    /* ---- shared controller, PHY and power (steps 1-12) ---- */
    STEP(APB_STEP_POWER);
    for (i = 0; i < 4; i++) CHECK(pmgr_enable(&c, pmgr_domains[i]));
    DONE();
    STEP(APB_STEP_AXI_TUNABLES);    CHECK(apply(&c, APB_AXI, &p->axi2af)); DONE();
    STEP(APB_STEP_RC_ZERO);         CHECK(wr(&c, APB_RC, RC_ZERO, 0, 0)); DONE();
    STEP(APB_STEP_COMMON_TUNABLES); CHECK(apply(&c, APB_RC, &p->common)); DONE();
    STEP(APB_STEP_PHY_TUNABLES);    CHECK(apply(&c, APB_PHY, &p->phy)); DONE();
    STEP(APB_STEP_PHY_CLK0);
    CHECK(mask32(&c, APB_PHY, PHY_CTRL, 0, PHY_CLK0REQ, 0));
    CHECK(poll(&c, APB_PHY, PHY_CTRL, PHY_CLK0ACK, PHY_CLK0ACK, PHY_CLK_TIMEOUT_US));
    DONE();
    STEP(APB_STEP_PHY_CLK1);
    CHECK(mask32(&c, APB_PHY, PHY_CTRL, 0, PHY_CLK1REQ, 0));
    CHECK(poll(&c, APB_PHY, PHY_CTRL, PHY_CLK1ACK, PHY_CLK1ACK, PHY_CLK_TIMEOUT_US));
    DONE();
    STEP(APB_STEP_PHY_RESET_RELEASE);
    CHECK(mask32(&c, APB_PHY, PHY_CTRL, PHY_RESET_T8103, 0, 0));
    delay(&c, 1);
    DONE();
    STEP(APB_STEP_PHYIF_RUN);
    CHECK(mask32(&c, APB_RC, RC_PHYIF_CTRL, 0, RC_PHYIF_RUN, 0));
    delay(&c, 1);
    DONE();
    STEP(APB_STEP_FUSES);                 CHECK(fuses(&c)); DONE();
    STEP(APB_STEP_PHYIP_PLL_TUNABLES);    CHECK(apply(&c, APB_PHYIP, &p->phy_ip_pll)); DONE();
    STEP(APB_STEP_PHYIP_AUSPMA_TUNABLES); CHECK(apply(&c, APB_PHYIP, &p->phy_ip_auspma)); DONE();

    /* ---- port 0 (steps 13-23) ---- */
    STEP(APB_STEP_PORT_CONFIG_TUNABLES); CHECK(apply(&c, APB_PORT0, &p->port_config)); DONE();
    STEP(APB_STEP_PORT_APPCLK);
    CHECK(mask32(&c, APB_PORT0, PORT_APPCLK, 0, PORT_APPCLK_EN, 0)); DONE();
    STEP(APB_STEP_PORT_PERST_RELEASE);
    CHECK(mask32(&c, APB_PORT0, PORT_RESET, 0, PORT_RESET_DIS, 0)); DONE();
    STEP(APB_STEP_PORT_RUN);
    CHECK(poll(&c, APB_PORT0, PORT_STATUS, PORT_STATUS_RUN, PORT_STATUS_RUN, PORT_TIMEOUT_US)); DONE();
    STEP(APB_STEP_PORT_IDLE);
    CHECK(poll(&c, APB_PORT0, PORT_LINKSTS, PORT_LINKSTS_BUSY, 0, PORT_TIMEOUT_US)); DONE();
    STEP(APB_STEP_DBI_WRITE_ENABLE);
    CHECK(mask32(&c, APB_ECAM0, DBI_RO_WR, 0, DBI_RO_WR_EN, 0)); DONE();
    STEP(APB_STEP_RC_TUNABLES);
    CHECK(apply(&c, APB_ECAM0, &p->rc));
    CHECK(apply(&c, APB_ECAM0, &p->rc_gen3));
    CHECK(apply(&c, APB_ECAM0, &p->rc_gen4));
    DONE();
    STEP(APB_STEP_LINK_SPEED);
    CHECK(mask32(&c, APB_ECAM0, CAP_LNKCAP, LNKCAP_SLS, speed, 0));
    CHECK(mask32(&c, APB_ECAM0, CAP_LNKCAP2, LNKCAP2_SLS, ((1u << speed) - 1u) << 1, 0));
    CHECK(mask16_lnkctl2(&c, LNKCTL2_TLS, (uint16_t)speed));
    CHECK(mask32(&c, APB_ECAM0, DBI_WIDTH_SPEED, 0, DBI_SPEED_CHANGE, 0));
    DONE();
    STEP(APB_STEP_LINK_WIDTH);          /* one lane on the J293 apcie */
    CHECK(mask32(&c, APB_ECAM0, DBI_PORT_LINK_CTRL, DBI_LINK_MODE, DBI_LINK_MODE_1, 0));
    CHECK(mask32(&c, APB_ECAM0, DBI_WIDTH_SPEED, DBI_LINK_WIDTH, 1u << 8, 0));
    CHECK(mask32(&c, APB_ECAM0, CAP_LNKCAP, LNKCAP_MLW, 1u << 4, 0));
    DONE();
    STEP(APB_STEP_DBI_WRITE_DISABLE);
    CHECK(mask32(&c, APB_ECAM0, DBI_RO_WR, DBI_RO_WR_EN, 0, 0)); DONE();
    STEP(APB_STEP_LINK_STATUS);
    CHECK(rd(&c, APB_PORT0, PORT_LINKSTS, &r->linksts)); DONE();
    e = APB_OK;
done:
    r->result = e;
    return e;
}
