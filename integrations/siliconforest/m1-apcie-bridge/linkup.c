/* Original darwin-lab port-0 link stage; see linkup.h, LINKUP.md, NOTICE-linkup. */
#include "linkup.h"
#include <string.h>

#if defined(KERNEL) && defined(APB_FAKE_REGISTER_FILE)
#error "APB_FAKE_REGISTER_FILE is host-test only: a kernel build must not contain shared writes"
#endif

#define APL_MAX_READS    20000u
#define APL_MAX_WRITES   512u
#define APL_POLL_US      100u

/* Port-0 PHY lane bank (APL_P0PHY) */
#define LANE_CFG             0x000u
#define LANE_REFCLK0_REQ     (1u << 0)
#define LANE_REFCLK1_REQ     (1u << 1)
#define LANE_REFCLK0_ACK     (1u << 2)
#define LANE_REFCLK1_ACK     (1u << 3)
#define LANE_REFCLK_EN       ((1u << 9) | (1u << 10))
#define LANE_CTL             0x004u
#define LANE_CTL_CONFIG      (1u << 15)
#define LANE_ACK_US          50000u

/* Port 0 block (APB_PORT0) */
#define P_LTSSM_CTL          0x080u
#define P_LTSSM_START        (1u << 0)
#define P_INT_STATUS         0x100u
#define P_INT_MASK           0x104u
#define P_MSI_CONFIG         0x124u
#define P_MSI_ENABLE         (1u << 0)
#define P_MSI_LOG2_SHIFT     4u
#define P_MSI_BASE           0x128u
#define P_MSI_DOORBELL       0x168u
#define P_LINK_STATUS        0x208u
#define P_LINK_UP            (1u << 0)
#define P_LINK_CMD_STATUS    0x210u
#define P_APP_CLOCK          0x800u
#define P_APP_CLOCK_ON       (1u << 0)
#define P_APP_CLOCK_NOGATE   (1u << 8)
#define P_STATUS             0x804u
#define P_STATUS_READY       (1u << 0)
#define P_REF_CLOCK          0x810u
#define P_REF_CLOCK_ON       (1u << 0)
#define P_REF_CLOCK_NOGATE   (1u << 8)
#define P_PERST              0x814u
#define P_PERST_RELEASED     (1u << 0)
#define P_READY_US           250000u
#define P_SID_TABLE          0x828u
#define P_SID_VALID          (1u << 31)
#define P_SID_SHIFT          16u
#define P_SID_MAX            0xfu
/* Probe value: valid bit clear, stream 0xf, requester 0x5a5a. */
#define P_SID_PROBE          0x000f5a5au

uint32_t apl_region_size(unsigned region)
{
    if (region == APL_P0PHY) return 0x4000u;
    return region < APB_REGIONS ? apb_region_size(region) : 0;
}

int apl_region_shared(unsigned region) { return region == APL_P0PHY || apb_region_shared(region); }

int apl_shared_writes_compiled(void)
{
#ifdef APB_FAKE_REGISTER_FILE
    return 1;
#else
    return 0;
#endif
}

static int sid_slot(uint32_t off)
{
    return off >= P_SID_TABLE && off < P_SID_TABLE + 4u * APL_RID2SID_MAX && !((off - P_SID_TABLE) & 3u);
}

static int write_ok(unsigned region, uint32_t off)
{
    if (region == APL_P0PHY) return off == LANE_CFG || off == LANE_CTL;
    if (region != APB_PORT0) return 0;
    switch (off) {
    case P_LTSSM_CTL: case P_INT_STATUS: case P_INT_MASK: case P_MSI_CONFIG: case P_MSI_BASE:
    case P_MSI_DOORBELL: case P_LINK_CMD_STATUS: case P_APP_CLOCK: case P_REF_CLOCK: case P_PERST:
        return 1;
    default:
        return sid_slot(off);
    }
}

static int read_ok(unsigned region, uint32_t off)
{
    if (region == APB_PORT0 && (off == P_LINK_STATUS || off == P_STATUS)) return 1;
    return write_ok(region, off);
}

struct lctx {
    const struct apb_io *io;
    const struct apl_pins *pins;
    struct apl_record *rec;
};

static int rd(struct lctx *c, unsigned region, uint32_t off, uint32_t *v)
{
    struct apl_record *r = c->rec;
    if (!read_ok(region, off)) return APB_ARGUMENT;
    r->last_region = region; r->last_offset = off;
    if (r->reads >= APL_MAX_READS) return APB_LIMIT;
    r->reads++;
    if (c->io->read32(c->io->context, region, off, v)) return APB_IO;
    r->last_value = *v;
    return APB_OK;
}

static int wr(struct lctx *c, unsigned region, uint32_t off, uint32_t v)
{
    struct apl_record *r = c->rec;
    if (!write_ok(region, off)) return APB_ARGUMENT;
    r->last_region = region; r->last_offset = off; r->last_value = v;
    if (r->shared_writes + r->port_writes + r->gated_writes + r->pin_calls >= APL_MAX_WRITES)
        return APB_LIMIT;
    if (apl_region_shared(region)) {
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

/* PERST# (which = 0) or power-enable (which = 1). Shared blocks: gated. */
static int pin(struct lctx *c, int which, int level)
{
    struct apl_record *r = c->rec;
    if (r->shared_writes + r->port_writes + r->gated_writes + r->pin_calls >= APL_MAX_WRITES)
        return APB_LIMIT;
#ifdef APB_FAKE_REGISTER_FILE
    {
        int (*fn)(void *, int) = which ? c->pins->power : c->pins->perst;
        if (!fn) return APB_IO;
        r->dirty = 1;
        r->pin_calls++;
        return fn(c->pins->context, level) ? APB_IO : APB_OK;
    }
#else
    (void)which; (void)level;
    r->gated_writes++;               /* no pin call is referenced on this path */
    return APB_GATED;
#endif
}

static int set_bits(struct lctx *c, unsigned region, uint32_t off, uint32_t bits)
{
    uint32_t v;
    int e = rd(c, region, off, &v);
    return e ? e : wr(c, region, off, v | bits);
}

static int clear_bits(struct lctx *c, unsigned region, uint32_t off, uint32_t bits)
{
    uint32_t v;
    int e = rd(c, region, off, &v);
    return e ? e : wr(c, region, off, v & ~bits);
}

static void wait_us(struct lctx *c, uint32_t us)
{
    if (c->io->delay_us) c->io->delay_us(c->io->context, us);
    c->rec->delay_us_total += us;
}

static int wait_bit(struct lctx *c, unsigned region, uint32_t off, uint32_t bit, uint32_t limit_us)
{
    uint32_t spent = 0, v;
    c->rec->polls++;
    for (;;) {
        int e = rd(c, region, off, &v);
        if (e) return e;
        if (v & bit) return APB_OK;
        if (spent >= limit_us) return APB_TIMEOUT;
        wait_us(c, APL_POLL_US);
        spent += APL_POLL_US;
    }
}

static unsigned log2_exact(uint32_t n)
{
    unsigned k = 0;
    while ((1u << k) < n) k++;
    return k;
}

int apl_plan_ok(const struct apl_plan *p)
{
    uint32_t i, j;
    if (!p) return 0;
    if (p->refclk_to_perst_us < 100u || p->refclk_to_perst_us > 10000u) return 0;
    if (p->perst_to_config_ms < 100u || p->perst_to_config_ms > 1000u) return 0;
    if (p->power_enable > 1u || (p->power_enable && (p->power_on_ms < 100u || p->power_on_ms > 1000u)))
        return 0;
    if (p->msi_vectors > 32u || (p->msi_vectors & (p->msi_vectors - 1u))) return 0;
    if (p->msi_vectors && (!p->msi_doorbell || (p->msi_doorbell & 3u))) return 0;
    if (!p->link_timeout_ms || p->link_timeout_ms > APL_MAX_LINK_MS) return 0;
    if (p->sid_count > APL_MAX_SIDS) return 0;
    for (i = 0; i < p->sid_count; i++) {
        if ((p->sids[i].rid >> 8) == 0 || p->sids[i].sid > P_SID_MAX) return 0;
        for (j = 0; j < i; j++) if (p->sids[j].rid == p->sids[i].rid) return 0;
    }
    return 1;
}

static int bringup_complete(const struct apb_record *b)
{
    return b && b->version == 1 && b->result == APB_OK && b->steps_completed == APB_STEP_COUNT - 1 &&
           b->gated_writes == 0 && b->shared_writes > 0 && b->dirty == 1;
}

static int sid_reset(struct lctx *c)
{
    uint32_t i, v;
    int e;
    for (i = 0; i < APL_RID2SID_MAX; i++) {
        uint32_t off = P_SID_TABLE + 4u * i;
        if ((e = wr(c, APB_PORT0, off, P_SID_PROBE))) return e;
        if ((e = rd(c, APB_PORT0, off, &v))) return e;
        if (v != P_SID_PROBE) {
            if (v && (e = wr(c, APB_PORT0, off, 0))) return e;
            break;
        }
        if ((e = wr(c, APB_PORT0, off, 0))) return e;
        if ((e = rd(c, APB_PORT0, off, &v))) return e;
        if (v) return APL_STATE;
    }
    c->rec->rid2sid_entries = i;
    return APB_OK;
}

static int sid_map(struct lctx *c, const struct apl_plan *p)
{
    uint32_t i, v;
    int e;
    if (p->sid_count > c->rec->rid2sid_entries) return APL_STATE;
    for (i = 0; i < p->sid_count; i++) {
        uint32_t off = P_SID_TABLE + 4u * i;
        uint32_t want = P_SID_VALID | ((uint32_t)p->sids[i].sid << P_SID_SHIFT) | p->sids[i].rid;
        if ((e = wr(c, APB_PORT0, off, want))) return e;
        if ((e = rd(c, APB_PORT0, off, &v))) return e;
        if (v != want) return APL_STATE;
    }
    return APB_OK;
}

#define STEP(id) do { r->step = (id); } while (0)
#define CHECK(expr) do { e = (expr); if (e) goto done; } while (0)
#define DONE() (r->steps_completed++)

int apl_link_port0(const struct apl_plan *p, const struct apb_io *io, const struct apl_pins *pins,
                   const struct apb_record *bringup, struct apl_record *r)
{
    struct lctx c;
    uint32_t v, settle;
    int e;

    if (!r) return APB_ARGUMENT;
    if (r->dirty) { r->result = APB_DIRTY; return APB_DIRTY; }
    memset(r, 0, sizeof(*r));
    r->version = 1;
    r->last_region = APL_REGIONS;
    r->result = APB_ARGUMENT;
    if (!p || !io || !io->read32 || !pins) return APB_ARGUMENT;
    if (!apl_plan_ok(p)) { r->result = APB_PLAN; return APB_PLAN; }
    c.io = io; c.pins = pins; c.rec = r;

    STEP(APL_STEP_PRECONDITION);
    if (!bringup_complete(bringup)) { e = APL_STATE; goto done; }
    CHECK(rd(&c, APB_PORT0, P_LINK_STATUS, &v));
    if (v & P_LINK_UP) { e = APL_STATE; goto done; }
    CHECK(rd(&c, APB_PORT0, P_APP_CLOCK, &v));
    if (!(v & P_APP_CLOCK_ON)) { e = APL_STATE; goto done; }
    DONE();

    STEP(APL_STEP_PERST_ASSERT);        CHECK(pin(&c, 0, 1)); DONE();
    STEP(APL_STEP_POWER_ENABLE);
    if (p->power_enable) CHECK(pin(&c, 1, 1));
    DONE();

    STEP(APL_STEP_LANE_CONFIG_OPEN);    CHECK(set_bits(&c, APL_P0PHY, LANE_CTL, LANE_CTL_CONFIG)); DONE();
    STEP(APL_STEP_LANE_REFCLK0);
    CHECK(set_bits(&c, APL_P0PHY, LANE_CFG, LANE_REFCLK0_REQ));
    CHECK(wait_bit(&c, APL_P0PHY, LANE_CFG, LANE_REFCLK0_ACK, LANE_ACK_US));
    DONE();
    STEP(APL_STEP_LANE_REFCLK1);
    CHECK(set_bits(&c, APL_P0PHY, LANE_CFG, LANE_REFCLK1_REQ));
    CHECK(wait_bit(&c, APL_P0PHY, LANE_CFG, LANE_REFCLK1_ACK, LANE_ACK_US));
    DONE();
    STEP(APL_STEP_LANE_CONFIG_CLOSE);   CHECK(clear_bits(&c, APL_P0PHY, LANE_CTL, LANE_CTL_CONFIG)); DONE();
    STEP(APL_STEP_LANE_REFCLK_ENABLE);  CHECK(set_bits(&c, APL_P0PHY, LANE_CFG, LANE_REFCLK_EN)); DONE();
    STEP(APL_STEP_PORT_REFCLK_ENABLE);  CHECK(set_bits(&c, APB_PORT0, P_REF_CLOCK, P_REF_CLOCK_ON)); DONE();

    STEP(APL_STEP_REFCLK_TO_PERST);
    settle = p->refclk_to_perst_us;
    if (p->power_enable && p->power_on_ms * 1000u > settle) settle = p->power_on_ms * 1000u;
    wait_us(&c, settle);
    DONE();
    STEP(APL_STEP_PERST_RELEASE);
    CHECK(set_bits(&c, APB_PORT0, P_PERST, P_PERST_RELEASED));
    CHECK(pin(&c, 0, 0));
    DONE();
    STEP(APL_STEP_PERST_TO_CONFIG);     wait_us(&c, p->perst_to_config_ms * 1000u); DONE();
    STEP(APL_STEP_PORT_READY);
    CHECK(wait_bit(&c, APB_PORT0, P_STATUS, P_STATUS_READY, P_READY_US));
    DONE();
    STEP(APL_STEP_CLOCK_GATING);
    CHECK(clear_bits(&c, APB_PORT0, P_REF_CLOCK, P_REF_CLOCK_NOGATE));
    CHECK(clear_bits(&c, APB_PORT0, P_APP_CLOCK, P_APP_CLOCK_NOGATE));
    DONE();
    STEP(APL_STEP_IRQ_QUIESCE);
    CHECK(wr(&c, APB_PORT0, P_INT_MASK, 0xffffffffu));
    CHECK(wr(&c, APB_PORT0, P_INT_STATUS, 0xffffffffu));
    CHECK(wr(&c, APB_PORT0, P_LINK_CMD_STATUS, 0xffffffffu));
    DONE();
    STEP(APL_STEP_MSI);
    if (p->msi_vectors) {
        CHECK(wr(&c, APB_PORT0, P_MSI_DOORBELL, p->msi_doorbell));
        CHECK(wr(&c, APB_PORT0, P_MSI_BASE, 0));
        CHECK(wr(&c, APB_PORT0, P_MSI_CONFIG,
                 (log2_exact(p->msi_vectors) << P_MSI_LOG2_SHIFT) | P_MSI_ENABLE));
    }
    DONE();
    STEP(APL_STEP_RID2SID_RESET);       CHECK(sid_reset(&c)); DONE();
    STEP(APL_STEP_RID2SID_MAP);         CHECK(sid_map(&c, p)); DONE();
    STEP(APL_STEP_LINK_TRAINING);       CHECK(wr(&c, APB_PORT0, P_LTSSM_CTL, P_LTSSM_START)); DONE();
    STEP(APL_STEP_LINK_UP);
    CHECK(wait_bit(&c, APB_PORT0, P_LINK_STATUS, P_LINK_UP, p->link_timeout_ms * 1000u));
    DONE();
    STEP(APL_STEP_LINK_STATUS);
    CHECK(rd(&c, APB_PORT0, P_LINK_STATUS, &r->linksts));
    CHECK(rd(&c, APB_PORT0, P_INT_STATUS, &r->intstat));
    DONE();
    e = APB_OK;
done:
    r->result = e;
    return e;
}
