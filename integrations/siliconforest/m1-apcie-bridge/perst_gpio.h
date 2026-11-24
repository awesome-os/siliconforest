/* Original darwin-lab PERST# model, fixed to J293 main AP GPIO 152.
 * Register facts: pinned MIT m1n1 trace/gpio.py; see PERST_GPIO.md.
 * Default and KERNEL builds never invoke GPIO callbacks. */
#ifndef DL_APCIE_PERST_GPIO_H
#define DL_APCIE_PERST_GPIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define APG_PERST_PIN 152u
#define APG_PERST_OFFSET (APG_PERST_PIN * 4u)

enum apg_result {
    APG_OK = 0, APG_GATED = 1, APG_NOT_READY = 2,
    APG_ARGUMENT = -1, APG_IO = -2, APG_LOCKED = -3, APG_VERIFY = -4
};

/* Host fake GPIO file only. No physical base, region, mapping, clock, delay,
 * reset/power helper or kernel writer is provided. Caller serializes access;
 * callbacks must not recurse. A callback is a C contract, not a sandbox. */
struct apg_io {
    void *context;
    int (*read32)(void *, uint32_t offset, uint32_t *value);
    int (*write32)(void *, uint32_t offset, uint32_t value);
};

/* One call's partial observation, not a pin-ownership or coherence claim.
 * dirty becomes 1 BEFORE attempting the sole fake write, including failed
 * writes. Failed reads leave the respective valid flag clear and value zero.
 * No rollback, retry, restore, pulse or timing assertion is implicit. */
struct apg_snapshot {
    uint32_t version;
    int32_t result;
    uint32_t asserted, reads_attempted, writes_attempted, gated;
    uint32_t before_valid, after_valid, before, after, dirty;
};

/* 1 only in explicit APG_FAKE_GPIO_FILE host-test builds; never KERNEL. */
int apg_fake_drive_compiled(void);

/* PERST# active-low model: asserted 1 -> DATA 0, asserted 0 -> DATA 1.
 * Host fake mode requires LOCK clear, PERIPH=0 and MODE=1 already selected;
 * it changes only DATA, preserving mux/direction/pull/group and other bits,
 * then checks DATA/LOCK/PERIPH/MODE readback once. Any mismatch fails.
 * No existing direction or mux is reconfigured. Default builds return
 * APG_GATED with zero reads/writes, even if callbacks are supplied.
 * snapshot is an output for this call and must not alias io or its context.
 * This is not wired into the existing link stage or any IOKit adapter. */
int apg_set_perst(const struct apg_io *io, int asserted, struct apg_snapshot *snapshot);

#ifdef __cplusplus
}
#endif
#endif
