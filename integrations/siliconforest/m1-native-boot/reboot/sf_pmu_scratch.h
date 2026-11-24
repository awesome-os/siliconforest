/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The boot-state bytes of the Apple SPMI PMU ("sera" on T8103) and the polled SPMI
 * controller access they need. Plain C with no kernel dependency: the kext supplies the
 * register accessors, the host test supplies a model of the controller.
 * Provenance: see sf_pmu_scratch.c.
 */
#ifndef _SF_PMU_SCRATCH_H
#define _SF_PMU_SCRATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Linux t8103.dtsi, nub_spmi / pmic1, and this machine's device tree
 * (/arm-io/nub-spmi reg 0, spmi-pmu reg and info-leg_scrpad). */
#define SF_T8103_NUB_SPMI_PHYS  0x23d0d9300ULL
#define SF_T8103_PMU_SID        0xf
#define SF_T8103_LEG_SCRPAD     0x9f00

/* offsets from the legacy scratchpad (Linux nvmem-layout of pmic1; m1n1 pmu.py: +2) */
#define SF_PMU_BOOT_STAGE       0x01    /* boot_stage: one byte */
#define SF_PMU_ERROR_COUNTS     0x02    /* boot_error_count: bits 0..3; panic_count: bits 4..7 */
#define SF_PMU_BOOT_ERROR_STAGE 0x03    /* boot_error_stage: one byte (named in the dtsi, unused by Linux) */
#define SF_PMU_SHUTDOWN_FLAG    0x0f    /* shutdown_flag: bit 3 */
#define SF_PMU_SHUTDOWN_BIT     0x08

/* Linux macsmc-reboot.c enum boot_stage */
#define SF_BOOT_STAGE_SHUTDOWN          0x00    /* clean shutdown */
#define SF_BOOT_STAGE_IBOOT_DONE        0x2f    /* last stage of the boot loader */
#define SF_BOOT_STAGE_KERNEL_STARTED    0x30    /* normal OS booting */

struct sf_spmi_io {
	void *ctx;
	uint32_t (*read32)(void *ctx, unsigned offset);
	void (*write32)(void *ctx, unsigned offset, uint32_t value);
	void (*delay_us)(void *ctx, unsigned us);
};

enum {
	SF_SPMI_OK = 0,
	SF_SPMI_BAD_ARGUMENT = -1,
	SF_SPMI_RX_STALE = -3,          /* the reply FIFO would not drain */
	SF_SPMI_TIMEOUT = -4,           /* no reply within 50 ms */
	SF_SPMI_WRONG_REPLY = -5,       /* a reply to another command or slave */
	SF_SPMI_SHORT_REPLY = -6,
	SF_SPMI_NOT_ACKED = -7,
	SF_SPMI_PARITY = -8,

	SF_SPMI_WRITE_REFUSED = -9,     /* the write gate: nothing was sent */
};

/* Extended register read long: 16-bit register address, 1..8 bytes. There is no write
 * entry point: the only SPMI write this file can send is one byte through the gate below. */
int sf_spmi_ext_readl(const struct sf_spmi_io *io, uint8_t sid, uint16_t addr, uint8_t *buf, unsigned len);

/*
 * The write gate (the model is m1-storage/kernel/sf_nvme_gate.c). This PMU also holds the
 * machine's regulator configuration; a stray write there cannot be taken back. A write is
 * sent only when ALL of this holds, checked at the one place that issues a write command:
 *   - the level is SF_PMU_WRITES_AS_LINUX (boot-arg sfpmu=2; 0 and 1 never write),
 *   - the slave is the T8103 PMU (0xf) and the register is one of the three bytes Linux's
 *     macsmc-reboot writes, by absolute address: 0x9f01 (boot_stage), 0x9f02 (the two
 *     counts), 0x9f0f (shutdown flag),
 *   - the bits that change are within that byte's cell: all of 0x9f01, bit 3 of 0x9f0f; in
 *     0x9f02 only bits may be cleared (the counts are only ever zeroed),
 *   - boot_stage is written with 0x00 or 0x30 only.
 * boot_error_stage (0x9f03) and pm_setting are read, never written (Linux does not either).
 * Every refusal is counted; the kext shows the counters by sysctl.
 */
enum {
	SF_PMU_NO_ACCESS = 0,
	SF_PMU_READ_ONLY = 1,
	SF_PMU_WRITES_AS_LINUX = 2,
};

enum {
	SF_PMU_GATE_OK = 0,
	SF_PMU_GATE_LEVEL = 1,          /* not sfpmu=2 */
	SF_PMU_GATE_SLAVE = 2,
	SF_PMU_GATE_ADDRESS = 3,
	SF_PMU_GATE_BITS = 4,           /* would change bits outside the cell */
	SF_PMU_GATE_VALUE = 5,
};

/* The decision alone, for tests: may `value` replace `old` at `addr` of slave `sid`? */
int sf_pmu_write_gate(unsigned level, uint8_t sid, uint16_t addr, uint8_t old, uint8_t value);

struct sf_pmu {
	const struct sf_spmi_io *io;
	uint8_t sid;
	uint16_t scrpad;
	uint16_t pm_setting;            /* register address, 0 when unknown: read for the log only */
	unsigned level;                 /* SF_PMU_*; anything but SF_PMU_WRITES_AS_LINUX never writes */
	uint32_t writes_sent;
	uint32_t writes_refused;
	uint32_t last_refusal;          /* SF_PMU_GATE_* << 16 | register address */
};

struct sf_pmu_state {
	uint8_t boot_stage;
	uint8_t boot_error_count;
	uint8_t panic_count;
	uint8_t boot_error_stage;
	uint8_t shutdown_flag;
	uint8_t pm_setting;
	uint8_t have_pm_setting;
};

/* Reads only. */
int sf_pmu_read_state(const struct sf_pmu *pmu, struct sf_pmu_state *state);

/*
 * What Linux macsmc_reboot_probe() writes: boot_stage = 0x30, then, only when one of the
 * two counts is not zero, panic_count = 0 and boot_error_count = 0 (each a
 * read-modify-write of its four bits, as the NVMEM cell writes are). `state` is what
 * sf_pmu_read_state() returned. Returns the first SPMI error, having attempted all writes.
 */
int sf_pmu_kernel_started(struct sf_pmu *pmu, const struct sf_pmu_state *state);

/*
 * What Linux macsmc_reboot_notify() writes before a restart or a power-off:
 * boot_stage = 0 and shutdown_flag = 0 (restart) or 1 (power-off).
 */
int sf_pmu_prepare_halt(struct sf_pmu *pmu, int poweroff);

#ifdef __cplusplus
}
#endif

#endif
