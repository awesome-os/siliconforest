/* Original strictly read-only port-0 observer. Every offset has m1n1 MIT source
 * evidence (NOTICE); the guard predicates are the m1-dart-survey guard's. */
#include "observe.h"
#include <string.h>

static const uint32_t power_offsets[4] = {
    APO_PMGR_IMX, APO_PMGR_APCIE, APO_PMGR_APCIE_GP, APO_PMGR_PCIE_REF
};

int apo_read_allowed(unsigned region, uint32_t offset)
{
    unsigned i;
    if (offset & 3u) return 0;
    switch (region) {
    case DLS_PMGR:
        for (i = 0; i < 4; i++) if (offset == power_offsets[i]) return 1;
        return 0;
    case DLS_PHY:
        return offset == APO_PHY_CTRL;
    case DLS_RC:
        return offset == APO_RC_PHYIF_CTRL;
    case DLS_PORT:
        return offset == APO_PORT_APPCLK || offset == APO_PORT_RESET ||
               offset == APO_PORT_STATUS || offset == APO_PORT_LINKSTS;
    default:
        return 0;   /* DLS_DART and anything else: never */
    }
}

static int read_one(const struct dls_io *io, struct apo_snapshot *s, unsigned region,
                    uint32_t offset, uint32_t *value, uint32_t valid_bit)
{
    s->last_region = region;
    s->last_offset = offset;
    if (!apo_read_allowed(region, offset)) return APO_ARGUMENT;
    if (s->reads_attempted >= APO_MAX_READS) return APO_LIMIT;
    s->reads_attempted++;
    if (io->read32(io->context, region, offset, value)) return APO_IO;
    s->valid_mask |= valid_bit;
    return APO_OBSERVED;
}

/* Same predicate as m1-dart-survey: TARGET=ACTUAL=0xf in the low byte, and no
 * reset, AUTO, PS-reset, BUSY/parent-off or device-disable bits. */
static int power_on(uint32_t v)
{
    return !(v & UINT32_C(0x90001c00)) && (v & 0xffu) == 0xffu;
}

static int power_stage(const struct dls_io *io, struct apo_snapshot *s, uint32_t *dst,
                       unsigned first_bit)
{
    unsigned i;
    for (i = 0; i < 4; i++) {
        int e = read_one(io, s, DLS_PMGR, power_offsets[i], &dst[i], 1u << (first_bit + i));
        if (e) return e;
        if (!power_on(dst[i])) return APO_POWER_NOT_READY;
    }
    return APO_OBSERVED;
}

int apo_observe(const struct dls_board *b, const struct dls_io *io, struct apo_snapshot *s)
{
    static const unsigned regions[4] = {DLS_PMGR, DLS_RC, DLS_PHY, DLS_PORT};
    static const uint64_t bases[4] = {
        UINT64_C(0x23b700000), UINT64_C(0x680000000), UINT64_C(0x680080000), UINT64_C(0x681000000)
    };
    unsigned i;
    int e, f;

    if (!s) return APO_ARGUMENT;
    memset(s, 0, sizeof(*s));
    s->version = 1;
    s->last_region = DLS_REGIONS;
    s->result = APO_ARGUMENT;
    if (!b || !io || !io->read32) return APO_ARGUMENT;
    for (i = 0; i < 4; i++) {
        if (b->base[regions[i]] != bases[i] || b->size[regions[i]] != 0x4000u) {
            s->result = APO_BOARD;
            return APO_BOARD;
        }
    }

    s->stage_reached = APO_STAGE_POWER;
    e = power_stage(io, s, s->power, 0);
    if (e) goto done;                       /* power not on: no other MMIO at all */

    s->stage_reached = APO_STAGE_PHY;
    e = read_one(io, s, DLS_PHY, APO_PHY_CTRL, &s->phy_ctrl, APO_V_PHY_CTRL);
    if (e) goto done;
    if ((s->phy_ctrl & 0x8fu) != 0x0fu) { e = APO_PHY_NOT_READY; goto final; }

    s->stage_reached = APO_STAGE_PHYIF;
    e = read_one(io, s, DLS_RC, APO_RC_PHYIF_CTRL, &s->rc_phyif_ctrl, APO_V_RC_PHYIF);
    if (e) goto done;
    if (!(s->rc_phyif_ctrl & 1u)) { e = APO_PHYIF_NOT_RUNNING; goto final; }

    /* Same block m1n1 first touches after PHYIF RUN. The m1-dart-survey guard
     * stops at the first clear bit because it gates DART reads; this observer
     * records all three control words of the block it has already reached. */
    s->stage_reached = APO_STAGE_PORT;
    e = read_one(io, s, DLS_PORT, APO_PORT_APPCLK, &s->port0_appclk, APO_V_PORT_APPCLK);
    if (e) goto done;
    e = read_one(io, s, DLS_PORT, APO_PORT_RESET, &s->port0_reset, APO_V_PORT_RESET);
    if (e) goto done;
    e = read_one(io, s, DLS_PORT, APO_PORT_STATUS, &s->port0_status, APO_V_PORT_STATUS);
    if (e) goto done;
    if (!(s->port0_status & 1u)) { e = APO_PORT_NOT_RUNNING; goto final; }

    /* m1n1 reads LINKSTS only after STATUS RUN. */
    s->stage_reached = APO_STAGE_LINK;
    e = read_one(io, s, DLS_PORT, APO_PORT_LINKSTS, &s->port0_linksts, APO_V_PORT_LINKSTS);
    if (e) goto done;
    e = APO_OBSERVED;

final:
    /* Do not report a stage result after power was lost underneath it. */
    f = power_stage(io, s, s->power_final, 10);
    if (f == APO_POWER_NOT_READY) e = APO_POWER_LOST;
    else if (f) e = f;
done:
    s->result = e;
    return e;
}

/* ---------- ADT-only port 0 binding ---------- */
static int prop(dls_property_fn get, void *ctx, const char *path, const char *key,
                const uint8_t **p, size_t *n)
{
    *p = 0; *n = 0;
    return !get(ctx, path, key, p, n) && *p && *n <= 32768;
}
static int prop_equals(dls_property_fn get, void *ctx, const char *path, const char *key,
                       const char *want, size_t len)
{
    const uint8_t *p;
    size_t n;
    return prop(get, ctx, path, key, &p, &n) && n == len && !memcmp(p, want, len);
}
static int node_absent(dls_property_fn get, void *ctx, const char *path)
{
    const uint8_t *p = 0;
    size_t n = 0;
    return get(ctx, path, "name", &p, &n) != 0;
}

int apo_validate_bridge(dls_property_fn get, void *ctx)
{
    static const char bridge[] = "apcie-bridge";                  /* 13 bytes with NUL */
    static const char wlan[] = "wlan-pcie,bcm4378\0wlan-pcie,bcm";  /* 32 bytes with NUL */
    const uint8_t *p;
    size_t n;
    if (!get) return APO_ARGUMENT;
    if (!prop_equals(get, ctx, "/arm-io/apcie/pci-bridge0", "compatible", bridge, sizeof bridge) ||
        !prop(get, ctx, "/arm-io/apcie/pci-bridge0", "function-perst", &p, &n) || !n ||
        !prop_equals(get, ctx, "/arm-io/apcie/pci-bridge0/wlan", "compatible", wlan, sizeof wlan) ||
        !node_absent(get, ctx, "/arm-io/apcie/pci-bridge1") ||
        !node_absent(get, ctx, "/arm-io/apcie/pci-bridge2"))
        return APO_BOARD;
    return APO_OBSERVED;
}
