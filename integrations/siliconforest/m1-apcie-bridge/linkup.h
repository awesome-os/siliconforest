/* Original darwin-lab port-0 link stage for the J293 apcie,t8103 (slice 3).
 * It runs after apb_bringup_port0 (slice 2) and adds the port steps that the
 * m1n1 sequence leaves to the OS driver: PERST# pin control, the lane
 * reference clock handshake, clock gating, interrupt quiesce, MSI doorbell,
 * the RID-to-SID table and link training. See LINKUP.md and NOTICE-linkup.
 * Pure C: no IOKit, no allocation, no globals.
 *
 * SAFETY GATE (same rule as bringup.h). Writes to the port-0 PHY lane bank
 * (inside the shared PHY window) and the PERST#/power-enable pin calls
 * (shared GPIO/SMC blocks) exist only under APB_FAKE_REGISTER_FILE, which a
 * KERNEL build refuses at compile time. The first write this stage attempts
 * is always the PERST# assert, so in a bundle it stops there with no write.
 * It also refuses to start unless given a completed slice-2 record with no
 * gated writes, which a bundle build can never produce. */
#ifndef DL_APCIE_LINKUP_H
#define DL_APCIE_LINKUP_H
#include "bringup.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Regions beyond slice 2's eight. APB_PORT0 keeps its slice-2 number. */
#define APL_P0PHY      8u   /* 0x680084000 size 0x4000 (ADT reg[8]), shared PHY window */
#define APL_REGIONS    9u

enum apl_step {
    APL_STEP_NONE = 0,
    APL_STEP_PRECONDITION,      /* slice-2 record complete; port not linked; APPCLK on */
    APL_STEP_PERST_ASSERT,      /* PERST# pin asserted (first write of this stage) */
    APL_STEP_POWER_ENABLE,      /* optional power-enable pin; J293 ADT declares none */
    APL_STEP_LANE_CONFIG_OPEN,  /* lane control: config access on */
    APL_STEP_LANE_REFCLK0,      /* lane refclk 0 request, wait for ack */
    APL_STEP_LANE_REFCLK1,      /* lane refclk 1 request, wait for ack */
    APL_STEP_LANE_CONFIG_CLOSE, /* lane control: config access off */
    APL_STEP_LANE_REFCLK_ENABLE,
    APL_STEP_PORT_REFCLK_ENABLE,
    APL_STEP_REFCLK_TO_PERST,   /* ADT t-refclk-to-perst (us) */
    APL_STEP_PERST_RELEASE,     /* port PERST off, then PERST# pin released */
    APL_STEP_PERST_TO_CONFIG,   /* ADT perst-to-config (ms) */
    APL_STEP_PORT_READY,
    APL_STEP_CLOCK_GATING,      /* allow refclk and appclk gating */
    APL_STEP_IRQ_QUIESCE,       /* mask all, clear status and link command status */
    APL_STEP_MSI,               /* doorbell, base 0, enable with log2(vectors) */
    APL_STEP_RID2SID_RESET,     /* probe table size, clear every entry */
    APL_STEP_RID2SID_MAP,       /* program the plan's requester-to-stream entries */
    APL_STEP_LINK_TRAINING,     /* LTSSM start */
    APL_STEP_LINK_UP,           /* wait for link up */
    APL_STEP_LINK_STATUS,       /* final link and interrupt status */
    APL_STEP_COUNT
};

#define APL_STATE           (-6)  /* hardware is not in the state slice 2 leaves */
#define APL_MAX_SIDS        8u
#define APL_RID2SID_MAX     64u
#define APL_MAX_LINK_MS     1000u

struct apl_sid_entry { uint16_t rid; uint16_t sid; };

struct apl_plan {
    uint32_t refclk_to_perst_us;   /* ADT pci-bridge0 t-refclk-to-perst; >= 100 */
    uint32_t perst_to_config_ms;   /* ADT pci-bridge0 perst-to-config; >= 100 */
    uint32_t power_enable;         /* 1 if the board declares a port power-enable pin */
    uint32_t power_on_ms;          /* settle time after power-enable; >= 100 when used */
    uint32_t msi_doorbell;         /* ADT apcie msi-address (low word; high word must be 0) */
    uint32_t msi_vectors;          /* power of two, 1..32; 0 leaves MSI unconfigured */
    uint32_t link_timeout_ms;      /* 1..1000 */
    uint32_t sid_count;            /* <= APL_MAX_SIDS */
    struct apl_sid_entry sids[APL_MAX_SIDS];
};

/* Board pins. Both live in shared blocks, so their calls are gated like
 * shared writes. asserted = 1 drives PERST# active (device held in reset). */
struct apl_pins {
    void *context;
    int (*perst)(void *, int asserted);
    int (*power)(void *, int on);
};

struct apl_record {
    uint32_t version;                    /* 1 */
    int32_t result;
    uint32_t step, steps_completed;
    uint32_t reads, shared_writes, port_writes, gated_writes, pin_calls, polls;
    uint64_t delay_us_total;
    uint32_t last_region, last_offset, last_value;
    uint32_t rid2sid_entries;            /* probed table size */
    uint32_t linksts, intstat;           /* valid when steps_completed == 21 */
    uint32_t dirty;                      /* 1 once any write or pin call was issued */
};

uint32_t apl_region_size(unsigned region);
int apl_region_shared(unsigned region);
/* 1 if this build can issue lane-bank writes and pin calls (host fake only). */
int apl_shared_writes_compiled(void);
/* Checks a plan without touching hardware. */
int apl_plan_ok(const struct apl_plan *plan);

/* Runs steps 1-21 once, after a completed apb_bringup_port0. Stops at the
 * first failure; no retry. rec must be zeroed; a dirty rec is refused. */
int apl_link_port0(const struct apl_plan *plan, const struct apb_io *io,
                   const struct apl_pins *pins, const struct apb_record *bringup,
                   struct apl_record *rec);

#ifdef __cplusplus
}
#endif
#endif
