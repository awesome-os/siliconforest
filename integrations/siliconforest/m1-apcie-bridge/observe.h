/* Original darwin-lab read-only observer for the J293 apcie,t8103 port 0.
 * Bridge skeleton, step W1(a) of reports/sprint-010/wifi-darwin-netif-gap-001.json.
 * Register facts: m1n1 src/pcie.c (MIT), see NOTICE. No code copied. */
#ifndef DL_APCIE_OBSERVE_H
#define DL_APCIE_OBSERVE_H
#include <stddef.h>
#include <stdint.h>
/* Frozen m1-dart-survey header, used unchanged: region ids, struct dls_board,
 * struct dls_io (read32 only), dls_property_fn and dls_validate_board. */
#include "survey.h"
#ifdef __cplusplus
extern "C" {
#endif

enum apo_result {
    APO_OBSERVED = 0,          /* all stages read; LINKSTS valid */
    APO_POWER_NOT_READY = 1,   /* a PMGR domain is not on; nothing else read */
    APO_PHY_NOT_READY = 2,     /* PHY clocks not acked or PHY reset asserted; RC and port not read */
    APO_PHYIF_NOT_RUNNING = 3, /* RC PHYIF RUN clear; port not read */
    APO_PORT_NOT_RUNNING = 4,  /* port0 STATUS RUN clear; LINKSTS not read */
    APO_POWER_LOST = 5,        /* final PMGR re-read refused; earlier fields kept */
    APO_ARGUMENT = -1, APO_BOARD = -2, APO_IO = -3, APO_LIMIT = -4
};
enum apo_stage {
    APO_STAGE_NONE = 0, APO_STAGE_POWER, APO_STAGE_PHY, APO_STAGE_PHYIF,
    APO_STAGE_PORT, APO_STAGE_LINK
};

/* Register offsets (read-only). PMGR words are the m1-dart-survey guard's. */
#define APO_PMGR_IMX        0x1b8u
#define APO_PMGR_APCIE      0x348u
#define APO_PMGR_APCIE_GP   0x3e8u
#define APO_PMGR_PCIE_REF   0x1a0u
#define APO_PHY_CTRL        0x000u   /* CLK0/1 REQ bits0/1, ACK bits2/3, T8103 reset bit7 */
#define APO_RC_PHYIF_CTRL   0x024u   /* RUN bit0 */
#define APO_PORT_LINKSTS    0x208u   /* UP bit0, BUSY bit2, L2 bit6 */
#define APO_PORT_APPCLK     0x800u   /* EN bit0 */
#define APO_PORT_STATUS     0x804u   /* RUN bit0 */
#define APO_PORT_RESET      0x814u   /* DIS bit0 = PERST deasserted */

#define APO_MAX_READS 14u            /* 4 power + PHY + PHYIF + 3 port + LINKSTS + 4 final */

#define APO_V_POWER(i)      (1u << (i))          /* bits 0..3 */
#define APO_V_PHY_CTRL      (1u << 4)
#define APO_V_RC_PHYIF      (1u << 5)
#define APO_V_PORT_APPCLK   (1u << 6)
#define APO_V_PORT_RESET    (1u << 7)
#define APO_V_PORT_STATUS   (1u << 8)
#define APO_V_PORT_LINKSTS  (1u << 9)
#define APO_V_FINAL(i)      (1u << (10 + (i)))   /* bits 10..13 */

/* Version-1 diagnostic, 84 bytes, all little-endian u32/s32 (see observe_layout.h).
 * A field is meaningful only if its valid_mask bit is set. */
struct apo_snapshot {
    uint32_t version, reads_attempted, stage_reached, valid_mask;
    int32_t result;
    uint32_t last_region, last_offset;
    uint32_t power[4], power_final[4];
    uint32_t phy_ctrl, rc_phyif_ctrl;
    uint32_t port0_appclk, port0_reset, port0_status, port0_linksts;
};

/* Port 0 binding beyond the frozen board profile: /arm-io/apcie/pci-bridge0 is
 * "apcie-bridge" with a function-perst property, its wlan child is exactly
 * "wlan-pcie,bcm4378\0wlan-pcie,bcm\0", and pci-bridge1/2 are absent.
 * ADT reads only. Returns APO_OBSERVED (0) or APO_BOARD. */
int apo_validate_bridge(dls_property_fn get, void *context);
/* 1 only for the ten whitelisted (region, offset) pairs above; never DLS_DART. */
int apo_read_allowed(unsigned region, uint32_t offset);
/* Staged, read-only observation aligned with the m1-dart-survey guard:
 * power (stop at the first domain not on) -> PHY CTRL -> RC PHYIF RUN ->
 * port0 APPCLK, RESET, STATUS -> LINKSTS only if STATUS RUN. After any read
 * past the power stage, the four PMGR words are read again. The interface has
 * only read32: no write, power, reset, retry, wait or link action exists. */
int apo_observe(const struct dls_board *board, const struct dls_io *io,
                struct apo_snapshot *out);

#ifdef __cplusplus
}
#endif
#endif
