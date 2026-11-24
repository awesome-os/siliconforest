/* Original darwin-lab port-0 bring-up engine for the J293 apcie,t8103.
 * Sequence rewritten from m1n1 src/pcie.c and src/pmgr.c (MIT; see
 * NOTICE-bringup). Pure C: no IOKit, no allocation, no globals.
 *
 * SAFETY GATE. Every write to a shared region (PMGR, RC, PHY, PHY-IP, AXI)
 * exists only when APB_FAKE_REGISTER_FILE is defined, which only the host test
 * build does. Kernel builds (KERNEL defined) refuse that macro at compile
 * time, so in a built bundle the shared-write path returns APB_GATED without
 * calling any writer. All shared steps precede the port steps, so a bundle run
 * stops at the first shared write with no MMIO write at all. */
#ifndef DL_APCIE_BRINGUP_H
#define DL_APCIE_BRINGUP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum apb_region {
    APB_PMGR = 0,   /* 0x23b700000, guard words only                      shared */
    APB_RC,         /* 0x680000000 size 0x4000                             shared */
    APB_PHY,        /* 0x680080000 prefix 0x10000 (common + port banks)    shared */
    APB_PHYIP,      /* 0x6800c0000 size 0x20000                            shared */
    APB_AXI,        /* 0x68c000000 size 0x4000                             shared */
    APB_FUSE,       /* 0x23d2bc000 size 0x1000, read-only                  shared */
    APB_PORT0,      /* 0x681000000 size 0x8000, port 0 config         port-local */
    APB_ECAM0,      /* 0x690000000 first 0x8000, port 0 RC config     port-local */
    APB_REGIONS
};

enum apb_result {
    APB_OK = 0,
    APB_GATED = 1,        /* a shared write was reached in a build without the fake register file */
    APB_TIMEOUT = 2,      /* a bounded poll did not see its value */
    APB_ARGUMENT = -1, APB_PLAN = -2, APB_IO = -3, APB_LIMIT = -4, APB_DIRTY = -5
};

/* Steps, in m1n1's T8103 order. 1-12 are shared, 13-23 are port 0. */
enum apb_step {
    APB_STEP_NONE = 0,
    APB_STEP_POWER, APB_STEP_AXI_TUNABLES, APB_STEP_RC_ZERO, APB_STEP_COMMON_TUNABLES,
    APB_STEP_PHY_TUNABLES, APB_STEP_PHY_CLK0, APB_STEP_PHY_CLK1, APB_STEP_PHY_RESET_RELEASE,
    APB_STEP_PHYIF_RUN, APB_STEP_FUSES, APB_STEP_PHYIP_PLL_TUNABLES, APB_STEP_PHYIP_AUSPMA_TUNABLES,
    APB_STEP_PORT_CONFIG_TUNABLES, APB_STEP_PORT_APPCLK, APB_STEP_PORT_PERST_RELEASE,
    APB_STEP_PORT_RUN, APB_STEP_PORT_IDLE, APB_STEP_DBI_WRITE_ENABLE, APB_STEP_RC_TUNABLES,
    APB_STEP_LINK_SPEED, APB_STEP_LINK_WIDTH, APB_STEP_DBI_WRITE_DISABLE, APB_STEP_LINK_STATUS,
    APB_STEP_COUNT
};
#define APB_LAST_SHARED_STEP APB_STEP_PHYIP_AUSPMA_TUNABLES

/* One validated ADT local tunable: 32-bit, aligned, inside its window,
 * value bits inside mask. Applied as RMW: (old & ~mask) | value. */
struct apb_tunable { uint32_t offset, mask, value; };
struct apb_tunables { const struct apb_tunable *items; uint32_t count; };
#define APB_MAX_TUNABLES 256u

struct apb_plan {
    struct apb_tunables axi2af;        /* apcie-axi2af-tunables -> AXI */
    struct apb_tunables common;        /* apcie-common-tunables -> RC */
    struct apb_tunables phy;           /* apcie-phy-tunables -> PHY */
    struct apb_tunables phy_ip_pll;    /* apcie-phy-ip-pll-tunables -> PHYIP */
    struct apb_tunables phy_ip_auspma; /* apcie-phy-ip-auspma-tunables -> PHYIP */
    struct apb_tunables port_config;   /* pci-bridge0 apcie-config-tunables -> PORT0 */
    struct apb_tunables rc;            /* pci-bridge0 pcie-rc-tunables -> ECAM0 */
    struct apb_tunables rc_gen3;       /* pcie-rc-gen3-shadow-tunables -> ECAM0 */
    struct apb_tunables rc_gen4;       /* pcie-rc-gen4-shadow-tunables -> ECAM0 */
    uint32_t max_link_speed;           /* resolved by apb_resolve_link_speed, 1..4 */
};

struct apb_io {
    void *context;
    int (*read32)(void *, unsigned region, uint32_t offset, uint32_t *value);
    int (*write32)(void *, unsigned region, uint32_t offset, uint32_t value);
    int (*read16)(void *, unsigned region, uint32_t offset, uint16_t *value);
    int (*write16)(void *, unsigned region, uint32_t offset, uint16_t value);
    void (*delay_us)(void *, uint32_t microseconds);
};

struct apb_record {
    uint32_t version;                   /* 1 */
    int32_t result;
    uint32_t step;                      /* last step started */
    uint32_t steps_completed;
    uint32_t reads, shared_writes, port_writes, gated_writes, polls;
    uint64_t delay_us_total;
    uint32_t last_region, last_offset, last_value;
    uint32_t linksts;                   /* valid when steps_completed == 23 */
    uint32_t dirty;                     /* 1 once any MMIO write was issued */
};

uint32_t apb_region_size(unsigned region);
int apb_region_shared(unsigned region);
/* 1 if this build can issue shared writes (host fake register file only). */
int apb_shared_writes_compiled(void);

/* Parses an ADT local-tunable property (24-byte records: u32 offset, u32 size,
 * u64 mask, u64 value) for the given window. Strict: size 4, aligned, inside
 * the window, mask and value 32-bit, value within mask, at most cap records. */
int apb_parse_tunables(const uint8_t *bytes, size_t length, unsigned region,
                       struct apb_tunable *out, uint32_t cap, uint32_t *count);
/* m1n1 rule: maximum-link-speed, or for 1 the wlan child's target- or
 * expected-link-speed when positive (0 = absent); clamp to 4; 0 is invalid. */
int apb_resolve_link_speed(uint32_t bridge_max, uint32_t child_target,
                           uint32_t child_expected, uint32_t *out);

/* Runs steps 1-23 once. Stops at the first failure; no retry. rec must be
 * zeroed by the caller; a record with dirty set is refused (APB_DIRTY). */
int apb_bringup_port0(const struct apb_plan *plan, const struct apb_io *io,
                      struct apb_record *rec);

#ifdef __cplusplus
}
#endif
#endif
