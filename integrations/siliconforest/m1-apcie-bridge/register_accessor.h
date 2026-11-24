/* Original darwin-lab host-test adapter. No MMIO implementation or ownership
 * capability: the caller supplies only the existing test's fake callbacks. */
#ifndef DL_APCIE_REGISTER_ACCESSOR_H
#define DL_APCIE_REGISTER_ACCESSOR_H
#if !defined(APB_HOST_TEST_ACCESSOR) || defined(KERNEL) || defined(APB_FAKE_REGISTER_FILE)
#error "register accessor requires host-test-only build with shared writes compiled out"
#endif
#include "linkup.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned, single-threaded, stable until every slice call has returned.
 * Bind copies callbacks, not registers. No physical address, mapping, device,
 * pin callback, allocation, sleep or real clock is accepted by this adapter.
 * A C callback is not a security boundary: fake provenance is a caller contract
 * checked in test_register_accessor.c against the existing test_linkup.c file. */
struct apr_accessor {
    struct apb_io fake;
    uint32_t bound;
};

/* Revision002: distinct endpoint address spaces, never aliases of APCIE
 * root-port ECAM/PHY. Available only on apr_bind_bcm_fake instances. BAR2
 * cap is a host software limit, not observed RAM/BAR sizing. */
enum apr_bcm_region { APR_BCM_CONFIG = APL_REGIONS, APR_BCM_BAR0, APR_BCM_BAR2 };
#define APR_BCM_CONFIG_BYTES 0x1000u
#define APR_BCM_BAR0_BYTES 0x8000u
#define APR_BCM_BAR2_BYTES 0x800000u

/* On failure both outputs remain unchanged. Outputs and fake must be distinct.
 * All accesses use the slices' region IDs and bounds. 16-bit access is only
 * ECAM0 LNKCTL2 at 0xa0; 32-bit writes there are refused to preserve LNKSTA2.
 * All shared-region writes are refused, including PMGR, RC, PHY, PHYIP, AXI,
 * FUSE and the lane bank. The slices keep their existing per-register policy.
 * Failed reads leave the caller's value unchanged, even if a fake clobbers its
 * temporary output before returning failure. Callback errors become APB_IO.
 * delay_us forwards only to the fake elapsed-time accumulator, never a clock. */
int apr_bind_fake(struct apr_accessor *accessor, const struct apb_io *fake,
                  struct apb_io *slice_io);

/* Explicit new mode; original apr_bind_fake continues to refuse the three
 * endpoint regions. Only word access is allowed there. CONFIG writes are
 * limited to window selector0x80 and mailbox doorbell0x98. */
int apr_bind_bcm_fake(struct apr_accessor *, const struct apb_io *, struct apb_io *);
/* Host provenance/type guard; context must be a live caller-owned accessor.
 * It is not a sandbox against forged arbitrary C pointers. */
int apr_bcm_io_bound(const struct apb_io *);

#ifdef __cplusplus
}
#endif
#endif
