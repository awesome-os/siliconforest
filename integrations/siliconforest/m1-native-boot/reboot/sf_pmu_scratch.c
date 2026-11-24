// SPDX-License-Identifier: GPL-2.0
/*
 * Polled access to the Apple SPMI controller (generation 1 register layout, T8103
 * /arm-io/nub-spmi) and the boot-state bytes of the PMU behind it.
 *
 * Ported from Linux (Omacom linux-aurora integration/aurora 6a08a08e):
 *   drivers/spmi/spmi-apple-controller.c (GPL-2.0, Copyright The Asahi Linux Contributors;
 *     apple_spmi_pack_cmd, spmi_raw_cmd without the interrupt path, the gen1 offsets),
 *   drivers/base/regmap/regmap-spmi.c (regmap_spmi_ext_read / _gather_write: registers
 *     above 0xff go by "extended register read/write long", at most 8 bytes),
 *   drivers/power/reset/macsmc-reboot.c (GPL-2.0-only OR MIT; what is written when),
 *   arch/arm64/boot/dts/apple/t8103.dtsi (pmic1 nvmem-layout: which byte and bits),
 * with the stale-FIFO and reply-identity checks of m1n1 src/spmi.c raw_command() (MIT).
 */
#include "sf_pmu_scratch.h"

#define SPMI_STATUS_REG         0x00
#define SPMI_CMD_REG            0x04
#define SPMI_RSP_REG            0x08

#define SPMI_STATUS_RX_EMPTY    (1u << 24)

#define SPMI_CMD_ACTIVE         (1u << 15)

#define SPMI_REPLY_PARITY(r)    ((r) >> 16)
#define SPMI_REPLY_ACK          (1u << 15)
#define SPMI_REPLY_SLAVE(r)     (((r) >> 8) & 0x7f)
#define SPMI_REPLY_CMD(r)       ((r) & 0xff)

#define SPMI_CMD_EXT_WRITEL     0x30
#define SPMI_CMD_EXT_READL      0x38

#define POLL_US                 10
#define TIMEOUT_US              50000           /* Linux REG_POLL_TIMEOUT_US */

static int
rx_empty(const struct sf_spmi_io *io)
{
	return (io->read32(io->ctx, SPMI_STATUS_REG) & SPMI_STATUS_RX_EMPTY) != 0;
}

static int
raw_cmd(const struct sf_spmi_io *io, uint8_t opc, uint8_t sid, uint16_t param,
    const uint8_t *wr, unsigned len_wr, uint8_t *rd, unsigned len_rd)
{
	uint32_t reply, word;
	unsigned i, j, waited;

	if (io == 0 || sid > 0xf || len_wr > 8 || len_rd > 8)
		return SF_SPMI_BAD_ARGUMENT;
	/* Two command shapes exist here: read long of 1..8 bytes, write long of one byte. */
	if (!((opc & 0xf8) == SPMI_CMD_EXT_READL && len_wr == 0 && len_rd == (unsigned)(opc & 7) + 1) &&
	    !(opc == SPMI_CMD_EXT_WRITEL && len_wr == 1 && len_rd == 0))
		return SF_SPMI_BAD_ARGUMENT;

	/* m1n1: never take a word that was already in the reply FIFO for this command's reply */
	for (i = 0; !rx_empty(io); i++) {
		if (i >= 64)
			return SF_SPMI_RX_STALE;
		(void)io->read32(io->ctx, SPMI_RSP_REG);
	}

	/* apple_spmi_pack_cmd() */
	io->write32(io->ctx, SPMI_CMD_REG, (uint32_t)opc | (uint32_t)sid << 8 | (uint32_t)param << 16 | SPMI_CMD_ACTIVE);
	for (i = 0; i < len_wr;) {
		word = 0;
		for (j = 0; j < 4 && i < len_wr; j++)
			word |= (uint32_t)wr[i++] << (8 * j);
		io->write32(io->ctx, SPMI_CMD_REG, word);
	}

	for (waited = 0; rx_empty(io); waited += POLL_US) {
		if (waited >= TIMEOUT_US)
			return SF_SPMI_TIMEOUT;
		io->delay_us(io->ctx, POLL_US);
	}
	reply = io->read32(io->ctx, SPMI_RSP_REG);
	if (SPMI_REPLY_CMD(reply) != opc || SPMI_REPLY_SLAVE(reply) != sid)
		return SF_SPMI_WRONG_REPLY;

	for (i = 0; i < len_rd;) {
		if (rx_empty(io))
			return SF_SPMI_SHORT_REPLY;
		word = io->read32(io->ctx, SPMI_RSP_REG);
		for (j = 0; j < 4 && i < len_rd; j++)
			rd[i++] = (uint8_t)(word >> (8 * j));
	}

	if (len_rd == 0 && !(reply & SPMI_REPLY_ACK))
		return SF_SPMI_NOT_ACKED;
	if (~SPMI_REPLY_PARITY(reply) & ((1u << len_rd) - 1))
		return SF_SPMI_PARITY;
	return SF_SPMI_OK;
}

int
sf_spmi_ext_readl(const struct sf_spmi_io *io, uint8_t sid, uint16_t addr, uint8_t *buf, unsigned len)
{
	if (buf == 0 || len < 1 || len > 8)
		return SF_SPMI_BAD_ARGUMENT;
	return raw_cmd(io, (uint8_t)(SPMI_CMD_EXT_READL | (len - 1)), sid, addr, 0, 0, buf, len);
}

static int
read8(const struct sf_pmu *pmu, uint16_t addr, uint8_t *value)
{
	return sf_spmi_ext_readl(pmu->io, pmu->sid, addr, value, 1);
}

int
sf_pmu_write_gate(unsigned level, uint8_t sid, uint16_t addr, uint8_t old, uint8_t value)
{
	uint8_t changed = (uint8_t)(old ^ value);

	if (level != SF_PMU_WRITES_AS_LINUX)
		return SF_PMU_GATE_LEVEL;
	if (sid != SF_T8103_PMU_SID)
		return SF_PMU_GATE_SLAVE;
	switch (addr) {
	case SF_T8103_LEG_SCRPAD + SF_PMU_BOOT_STAGE:
		if (value != SF_BOOT_STAGE_SHUTDOWN && value != SF_BOOT_STAGE_KERNEL_STARTED)
			return SF_PMU_GATE_VALUE;
		return SF_PMU_GATE_OK;
	case SF_T8103_LEG_SCRPAD + SF_PMU_ERROR_COUNTS:
		/* the counts are only ever zeroed: no bit may be set */
		if (value & ~old)
			return SF_PMU_GATE_BITS;
		return SF_PMU_GATE_OK;
	case SF_T8103_LEG_SCRPAD + SF_PMU_SHUTDOWN_FLAG:
		if (changed & ~SF_PMU_SHUTDOWN_BIT)
			return SF_PMU_GATE_BITS;
		return SF_PMU_GATE_OK;
	default:
		return SF_PMU_GATE_ADDRESS;
	}
}

/*
 * The only place in this file (and in the kext: nothing else includes an SPMI write) that
 * issues a write command: one byte, read back first, through the gate. With any level but
 * SF_PMU_WRITES_AS_LINUX it returns before the controller is touched at all.
 */
static int
gated_write8(struct sf_pmu *pmu, uint16_t addr, uint8_t mask, uint8_t value)
{
	uint8_t old = 0, next;
	int ret, verdict;

	verdict = sf_pmu_write_gate(pmu->level, pmu->sid, addr, 0, 0);
	if (verdict == SF_PMU_GATE_LEVEL || verdict == SF_PMU_GATE_SLAVE || verdict == SF_PMU_GATE_ADDRESS)
		goto refused;
	ret = read8(pmu, addr, &old);
	if (ret != SF_SPMI_OK)
		return ret;
	next = (uint8_t)((old & ~mask) | (value & mask));
	verdict = sf_pmu_write_gate(pmu->level, pmu->sid, addr, old, next);
	if (verdict != SF_PMU_GATE_OK)
		goto refused;
	pmu->writes_sent++;
	return raw_cmd(pmu->io, (uint8_t)(SPMI_CMD_EXT_WRITEL | 0), pmu->sid, addr, &next, 1, 0, 0);
refused:
	pmu->writes_refused++;
	pmu->last_refusal = (uint32_t)verdict << 16 | addr;
	return SF_SPMI_WRITE_REFUSED;
}

int
sf_pmu_read_state(const struct sf_pmu *pmu, struct sf_pmu_state *state)
{
	uint8_t counts = 0, flag = 0;
	int ret;

	if (pmu == 0 || state == 0)
		return SF_SPMI_BAD_ARGUMENT;
	state->have_pm_setting = 0;
	state->pm_setting = 0;
	if ((ret = read8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_BOOT_STAGE), &state->boot_stage)) != SF_SPMI_OK ||
	    (ret = read8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_ERROR_COUNTS), &counts)) != SF_SPMI_OK ||
	    (ret = read8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_BOOT_ERROR_STAGE), &state->boot_error_stage)) != SF_SPMI_OK ||
	    (ret = read8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_SHUTDOWN_FLAG), &flag)) != SF_SPMI_OK)
		return ret;
	state->boot_error_count = counts & 0xf;
	state->panic_count = counts >> 4;
	state->shutdown_flag = (flag & SF_PMU_SHUTDOWN_BIT) != 0;
	if (pmu->pm_setting != 0 && read8(pmu, pmu->pm_setting, &state->pm_setting) == SF_SPMI_OK)
		state->have_pm_setting = 1;
	return SF_SPMI_OK;
}

int
sf_pmu_kernel_started(struct sf_pmu *pmu, const struct sf_pmu_state *state)
{
	int first, ret;

	if (pmu == 0 || state == 0)
		return SF_SPMI_BAD_ARGUMENT;
	first = gated_write8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_BOOT_STAGE), 0xff, SF_BOOT_STAGE_KERNEL_STARTED);
	if (state->boot_error_count == 0 && state->panic_count == 0)
		return first;
	/* macsmc_power_init_error_counts(): the panic count first, then the boot errors */
	ret = gated_write8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_ERROR_COUNTS), 0xf0, 0);
	if (first == SF_SPMI_OK)
		first = ret;
	ret = gated_write8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_ERROR_COUNTS), 0x0f, 0);
	if (first == SF_SPMI_OK)
		first = ret;
	return first;
}

int
sf_pmu_prepare_halt(struct sf_pmu *pmu, int poweroff)
{
	int first, ret;

	if (pmu == 0)
		return SF_SPMI_BAD_ARGUMENT;
	first = gated_write8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_BOOT_STAGE), 0xff, SF_BOOT_STAGE_SHUTDOWN);
	ret = gated_write8(pmu, (uint16_t)(pmu->scrpad + SF_PMU_SHUTDOWN_FLAG), SF_PMU_SHUTDOWN_BIT,
	    poweroff ? SF_PMU_SHUTDOWN_BIT : 0);
	if (first == SF_SPMI_OK)
		first = ret;
	return first;
}
