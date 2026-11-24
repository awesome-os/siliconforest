// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCO clock, MCA cluster and ADMAC transmit channel of T8103 for stereo playback.
 *
 * Ported from the Omacom Linux tree:
 *   sound/soc/apple/mca.c (cluster and port registers, the serialiser reset before a
 *     start, the clock enable order), Copyright (C) The Asahi Linux Contributors
 *   drivers/dma/apple-admac.c (channel registers, descriptor and report rings, SRAM
 *     carve-out, FIFO thresholds), Copyright (C) The Asahi Linux Contributors
 *   drivers/clk/clk-apple-nco.c (divider encoding), Copyright (C) The Asahi Linux
 *     Contributors (GPL-2.0-only OR MIT)
 * The device tree paths and the reset of the audio power domain follow m1n1
 * proxyclient/experiments/speaker_amp.py (MIT).
 */
#include "sf_mca.h"

#include <IOKit/IOLib.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFMca: " fmt "\n", ##__VA_ARGS__)

/* ---- NCO (clk-apple-nco.c) ---- */
#define NCO_CHANNEL_STRIDE 0x4000
#define NCO_REG_CTRL    0
#define NCO_CTRL_ENABLE (1u << 31)
#define NCO_REG_DIV     4
#define NCO_REG_INC1    8
#define NCO_REG_INC2    12
#define NCO_REG_ACCINIT 16
#define LFSR_POLY       0xa01
#define LFSR_INIT       0x7ff
#define LFSR_LEN        11
#define LFSR_PERIOD     ((1 << LFSR_LEN) - 1)
#define LFSR_TBLSIZE    (1 << LFSR_LEN)
#define COARSE_DIV_OFFSET 2

/* ---- MCA (mca.c), relative to the cluster ---- */
#define REG_STATUS              0x0
#define STATUS_MCLK_EN          (1u << 0)
#define REG_MCLK_CONF           0x4
#define REG_SYNCGEN_STATUS      0x100
#define SYNCGEN_STATUS_EN       (1u << 0)
#define REG_SYNCGEN_MCLK_SEL    0x104
#define REG_SYNCGEN_HI_PERIOD   0x108
#define REG_SYNCGEN_LO_PERIOD   0x10c
#define REG_PORT_ENABLES        0x600
#define PORT_ENABLES_CLOCKS     (3u << 1)
#define PORT_ENABLES_TX_DATA    (1u << 3)
#define REG_PORT_CLOCK_SEL      0x604
#define REG_PORT_DATA_SEL       0x608
#define PORT_DATA_SEL_TXA(cl)   (1u << ((cl) * 2))
#define REG_INTSTATE            0x700
#define REG_INTMASK             0x704
#define CLUSTER_TX_OFF          0x300
#define CLUSTER_RXB_OFF         0x400
#define REG_RX_SERDES_PORT      0x04
#define REG_RX_SERDES_CONF      0x08
#define REG_RX_SERDES_BITSTART  0x0c
#define REG_RX_SERDES_SLOTMASK  0x10
#define SERDES_CONF_WIDTH_16BIT 0x40
#define SERDES_CONF_NO_DATA_FEEDBACK (1u << 15)
#define REG_DMA_ADAPTER_B(cl)   (0x8000 * (cl) + 0x4000)
#define REG_SERDES_STATUS       0x00
#define SERDES_STATUS_EN        (1u << 0)
#define SERDES_STATUS_RST       (1u << 1)
#define REG_TX_SERDES_CONF      0x04
#define SERDES_CONF_NCHANS      0x0000000fu
#define SERDES_CONF_WIDTH_MASK  0x000001f0u
#define SERDES_CONF_WIDTH_32BIT 0x100
#define SERDES_CONF_BCLK_POL    0x400
#define SERDES_CONF_UNK1        (1u << 12)
#define SERDES_CONF_UNK2        (1u << 13)
#define SERDES_CONF_UNK3        (1u << 14)
#define SERDES_CONF_SYNC_SEL    0x00070000u
#define REG_TX_SERDES_BITSTART  0x08
#define REG_TX_SERDES_SLOTMASK  0x0c
#define CLUSTER_STRIDE          0x4000
/* relative to the switch */
#define REG_DMA_ADAPTER_A(cl)   (0x8000 * (cl))

/* ---- ADMAC (apple-admac.c) ---- */
#define RING_FULL               (1u << 9)
#define RING_EMPTY              (1u << 8)
#define RING_ERR                (1u << 10)
#define STATUS_DESC_DONE        (1u << 0)
#define STATUS_ERR              (1u << 6)
#define FLAG_DESC_NOTIFY        (1u << 16)
#define REG_TX_START            0x0000
#define REG_TX_STOP             0x0004
#define REG_RX_START            0x0008
#define REG_RX_STOP             0x000c
#define REG_TX_SRAM_SIZE        0x0094
#define REG_CHAN_BASE(ch)       (0x8000 + (ch) * 0x200)
#define REG_CHAN_CTL(ch)        (REG_CHAN_BASE(ch) + 0x00)
#define REG_CHAN_CTL_RST_RINGS  (1u << 0)
#define REG_CHAN_INTSTATUS(ch, idx) (REG_CHAN_BASE(ch) + 0x10 + (idx) * 4)
#define REG_CHAN_INTMASK(ch, idx)   (REG_CHAN_BASE(ch) + 0x20 + (idx) * 4)
#define REG_BUS_WIDTH(ch)       (REG_CHAN_BASE(ch) + 0x40)
#define BUS_WIDTH_16BIT         0x01
#define BUS_WIDTH_32BIT         0x02
#define BUS_WIDTH_FRAME_2_WORDS 0x10
#define BUS_WIDTH_FRAME_4_WORDS 0x20
#define REG_CHAN_SRAM_CARVEOUT(ch) (REG_CHAN_BASE(ch) + 0x50)
#define REG_CHAN_FIFOCTL(ch)    (REG_CHAN_BASE(ch) + 0x54)
#define REG_RESIDUE(ch)         (REG_CHAN_BASE(ch) + 0x64)
#define REG_DESC_RING(ch)       (REG_CHAN_BASE(ch) + 0x70)
#define REG_REPORT_RING(ch)     (REG_CHAN_BASE(ch) + 0x74)
#define REG_DESC_WRITE(ch)      (0x10000 + ((ch) / 2) * 0x4 + ((ch) & 1) * 0x4000)
#define REG_REPORT_READ(ch)     (0x10100 + ((ch) / 2) * 0x4 + ((ch) & 1) * 0x4000)
#define SRAM_BLOCK              2048
/* The interrupt output this SoC wires up (irq-destination-index); status is read from it. */
#define ADMAC_IRQ_INDEX         1

#define SLOT_WIDTH   32

static uint16_t gNcoFwd[LFSR_TBLSIZE];
static bool gNcoTable;

/* applnco_compute_tables(): coarse divisor to LFSR state */
static void
nco_tables(void)
{
	uint32_t state = LFSR_INIT;
	if (gNcoTable)
		return;
	for (int i = LFSR_PERIOD; i > 0; i--) {
		if (state & 1)
			state = (state >> 1) ^ (LFSR_POLY >> 1);
		else
			state = (state >> 1);
		gNcoFwd[i] = (uint16_t)state;
	}
	gNcoFwd[0] = 0;
	gNcoTable = true;
}

/* applnco_set_rate() followed by an enable */
static int
nco_start(const SFMmio &nco, unsigned chan, uint32_t parent, uint32_t rate)
{
	uint32_t base = chan * NCO_CHANNEL_STRIDE;
	uint32_t div = (uint32_t)(2ULL * parent / rate);
	uint32_t inc1 = (uint32_t)(2ULL * parent - (uint64_t)div * rate);
	uint32_t inc2 = inc1 - rate;
	uint32_t coarse = div / 4;

	if (coarse < COARSE_DIV_OFFSET || coarse >= COARSE_DIV_OFFSET + LFSR_TBLSIZE)
		return EINVAL;
	nco_tables();
	nco.w32(base + NCO_REG_CTRL, nco.r32(base + NCO_REG_CTRL) & ~NCO_CTRL_ENABLE);
	nco.w32(base + NCO_REG_DIV, ((uint32_t)gNcoFwd[coarse - COARSE_DIV_OFFSET] << 2) | (div % 4));
	nco.w32(base + NCO_REG_INC1, inc1);
	nco.w32(base + NCO_REG_INC2, inc2);
	nco.w32(base + NCO_REG_ACCINIT, 1u << 31);
	nco.w32(base + NCO_REG_CTRL, nco.r32(base + NCO_REG_CTRL) | NCO_CTRL_ENABLE);
	return 0;
}

/* The DMA controller and, through its parents, SIO and AUDIO_P; reset once, as when Linux probes it. */
static bool gAudioPowered;

static int
audio_power(void)
{
	if (gAudioPowered)
		return 0;
	if (sf_pmgr_enable_node("/arm-io/admac-sio", -1, 0, true) != 0)
		return ENXIO;
	if (sf_pmgr_reset_named("AUDIO_P", true) != 0)
		return ENXIO;
	gAudioPowered = true;
	return 0;
}

/* ---- ADMAC channel ---- */

void
SFAdmacChan::configure(unsigned wordBytes, unsigned frameWords, unsigned sramBlock)
{
	/* admac_alloc_chan_resources(), admac_device_config() */
	stop();
	admac->w32(REG_CHAN_INTMASK(ch, ADMAC_IRQ_INDEX), 0);
	admac->w32(REG_CHAN_SRAM_CARVEOUT(ch), (SRAM_BLOCK << 16) | (sramBlock * SRAM_BLOCK));
	admac->w32(REG_BUS_WIDTH(ch), (admac->r32(REG_BUS_WIDTH(ch)) & ~0xffu) |
	    (wordBytes == 2 ? BUS_WIDTH_16BIT : BUS_WIDTH_32BIT) |
	    (frameWords == 4 ? BUS_WIDTH_FRAME_4_WORDS : BUS_WIDTH_FRAME_2_WORDS));
	admac->w32(REG_CHAN_FIFOCTL(ch), ((0x30u * wordBytes) << 16) | (0x18u * wordBytes));
}

void
SFAdmacChan::resetRings(void)
{
	admac->w32(REG_CHAN_CTL(ch), REG_CHAN_CTL_RST_RINGS);
	admac->w32(REG_CHAN_CTL(ch), 0);
}

bool
SFAdmacChan::descRingFull(void) const
{
	return (admac->r32(REG_DESC_RING(ch)) & RING_FULL) != 0;
}

bool
SFAdmacChan::descRingEmpty(void) const
{
	return (admac->r32(REG_DESC_RING(ch)) & RING_EMPTY) != 0;
}

void
SFAdmacChan::submit(uint64_t iova, uint32_t bytes)
{
	admac->w32(REG_DESC_WRITE(ch), (uint32_t)iova);
	admac->w32(REG_DESC_WRITE(ch), (uint32_t)(iova >> 32));
	admac->w32(REG_DESC_WRITE(ch), bytes);
	admac->w32(REG_DESC_WRITE(ch), FLAG_DESC_NOTIFY);
}

void
SFAdmacChan::start(void)
{
	/* admac_start_chan(); the status bits are polled, no interrupt is unmasked */
	admac->w32(REG_CHAN_INTSTATUS(ch, ADMAC_IRQ_INDEX), STATUS_DESC_DONE | STATUS_ERR);
	admac->w32((ch & 1) ? REG_RX_START : REG_TX_START, 1u << (ch / 2));
}

bool
SFAdmacChan::enabled(void) const
{
	return (admac->r32((ch & 1) ? REG_RX_START : REG_TX_START) & (1u << (ch / 2))) != 0;
}

void
SFAdmacChan::stop(void)
{
	admac->w32((ch & 1) ? REG_RX_STOP : REG_TX_STOP, 1u << (ch / 2));
	/* admac_wait_stopped(): the channel finishes the burst in flight */
	for (int i = 0; i < 1000 && enabled(); i++)
		IODelay(100);
}

unsigned
SFAdmacChan::drainReports(uint64_t *counter)
{
	unsigned n = 0;

	/* admac_drain_reports(): four words per report */
	while (n < 8 && !(admac->r32(REG_REPORT_RING(ch)) & RING_EMPTY)) {
		uint32_t lo = admac->r32(REG_REPORT_READ(ch));
		uint32_t hi = admac->r32(REG_REPORT_READ(ch));
		(void)admac->r32(REG_REPORT_READ(ch));
		(void)admac->r32(REG_REPORT_READ(ch));
		if (counter != NULL)
			*counter = ((uint64_t)hi << 32) | lo;
		n++;
	}
	admac->w32(REG_CHAN_INTSTATUS(ch, ADMAC_IRQ_INDEX), STATUS_DESC_DONE);
	return n;
}

bool
SFAdmacChan::clearError(void)
{
	bool seen = false;

	/* admac_handle_status_err() */
	if (admac->r32(REG_DESC_RING(ch)) & RING_ERR) {
		admac->w32(REG_DESC_RING(ch), RING_ERR);
		seen = true;
	}
	if (admac->r32(REG_REPORT_RING(ch)) & RING_ERR) {
		admac->w32(REG_REPORT_RING(ch), RING_ERR);
		seen = true;
	}
	if (admac->r32(REG_CHAN_INTSTATUS(ch, ADMAC_IRQ_INDEX)) & STATUS_ERR) {
		admac->w32(REG_CHAN_INTSTATUS(ch, ADMAC_IRQ_INDEX), STATUS_ERR);
		seen = true;
	}
	return seen;
}

/* ---- playback stream ---- */

int
SFMca::attach(unsigned feCluster, unsigned portMask, unsigned slotsPerFrame, unsigned channelCount, unsigned sram)
{
	uint32_t nclusters = 0, clockId = 0;

	if (attached)
		return 0;
	if (!sf_adt_u32("/arm-io/mca-switch", "numClusters", 0, &nclusters))
		nclusters = sf_adt_cells("/arm-io/mca-switch", "clock-gates");
	/* clusters 4 and 5 share an NCO channel in the Linux device tree: keep to 0..3 */
	if (feCluster >= nclusters || feCluster > 3 || portMask == 0 || (portMask >> nclusters) != 0 ||
	    (slotsPerFrame != 2 && slotsPerFrame != 8) || (channelCount != 2 && channelCount != 4) ||
	    channelCount > slotsPerFrame || sram > 7) {
		LOG("cluster %u / ports 0x%x / %u slots / %u channels not supported", feCluster, portMask, slotsPerFrame,
		    channelCount);
		return EINVAL;
	}
	fe = feCluster;
	ports = portMask;
	slots = slotsPerFrame;
	channels = channelCount;
	sramBlock = sram;

	if (audio_power() != 0)
		return ENXIO;
	if (!nco.mapNode("/arm-io/nco", 0) || !mca.mapNode("/arm-io/mca-switch", 0) ||
	    !sw.mapNode("/arm-io/mca-switch", 1) || !admac.mapNode("/arm-io/admac-sio", 0))
		return ENXIO;
	tx.admac = &admac;
	tx.ch = 4 * fe;

	/* m1n1 hw/nco.py: the reference is /arm-io clock-frequencies[clock-ids[0] - 256] */
	ncoRefHz = 0;
	if (sf_adt_u32("/arm-io/nco", "clock-ids", 0, &clockId) && clockId >= 256)
		sf_adt_u32("/arm-io", "clock-frequencies", clockId - 256, &ncoRefHz);
	if (ncoRefHz < 100000000 || ncoRefHz > 2000000000) {
		LOG("no usable NCO reference in the device tree (clock id %u, %u Hz); assuming 900 MHz", clockId, ncoRefHz);
		ncoRefHz = 900000000;
	}
	LOG("nco 0x%llx (reference %u Hz), clusters 0x%llx, switch 0x%llx, admac 0x%llx; cluster %u -> ports 0x%x, "
	    "%u slots, %u channels, DMA channel %u, tx sram %u bytes", nco.phys, ncoRefHz, mca.phys, sw.phys, admac.phys,
	    fe, ports, slots, channels, tx.ch, admac.r32(REG_TX_SRAM_SIZE));
	attached = true;
	return 0;
}

int
SFMca::startClocks(uint32_t rate)
{
	const uint32_t cl = fe * CLUSTER_STRIDE, txo = cl + CLUSTER_TX_OFF;
	const uint32_t bclkRatio = slots * SLOT_WIDTH;
	int ret;

	if (!attached || clocksOn)
		return attached ? 0 : ENXIO;

	/*
	 * The NCO first: the cluster's power domain needs its clock to come up. Linux
	 * programs the cluster before it powers it; here every cluster register is written
	 * with the domain on.
	 */
	ret = nco_start(nco, fe, ncoRefHz, rate * bclkRatio);
	if (ret != 0) {
		LOG("NCO cannot make %u Hz from %u Hz", rate * bclkRatio, ncoRefHz);
		return ret;
	}
	ret = sf_pmgr_enable_node("/arm-io/mca-switch", (int)fe, kSFPmgrExternallyClocked, false);
	if (ret != 0)
		return ret;

	/* mca_be_startup(): each port takes data and clocks from the front-end cluster */
	for (unsigned p = 0; p < 6; p++) {
		const uint32_t pt = p * CLUSTER_STRIDE;
		if (!(ports & (1u << p)))
			continue;
		mca.w32(pt + REG_PORT_DATA_SEL, PORT_DATA_SEL_TXA(fe));
		mca.mask32(pt + REG_PORT_ENABLES, 0, PORT_ENABLES_TX_DATA);
		mca.w32(pt + REG_PORT_CLOCK_SEL, (fe + 1) << 8);
		mca.mask32(pt + REG_PORT_ENABLES, 0, PORT_ENABLES_CLOCKS);
	}

	/*
	 * mca_fe_set_fmt() for I2S with inverted bit clock and frame (MACAUDIO_DAI_FMT):
	 * no BCLK_POL, data starts one bit after the frame edge.
	 */
	mca.mask32(txo + REG_TX_SERDES_CONF, SERDES_CONF_BCLK_POL, 0);
	mca.w32(txo + REG_TX_SERDES_BITSTART, 1);

	/* mca_configure_serdes(): 32-bit slots, the first `channels` of them carry a channel */
	mca.mask32(txo + REG_TX_SERDES_CONF,
	    SERDES_CONF_WIDTH_MASK | SERDES_CONF_NCHANS | SERDES_CONF_SYNC_SEL | SERDES_CONF_UNK1 | SERDES_CONF_UNK2 |
	    SERDES_CONF_UNK3,
	    (slots - 1) | SERDES_CONF_WIDTH_32BIT | ((fe + 1) << 16) | SERDES_CONF_UNK1 | SERDES_CONF_UNK2 | SERDES_CONF_UNK3);
	mca.w32(txo + REG_TX_SERDES_SLOTMASK, 0xffffffff);
	mca.w32(txo + REG_TX_SERDES_SLOTMASK + 0x4, ~((1u << channels) - 1));
	mca.w32(txo + REG_TX_SERDES_SLOTMASK + 0x8, 0xffffffff);
	mca.w32(txo + REG_TX_SERDES_SLOTMASK + 0xc, ~((1u << slots) - 1));

	/* mca_fe_hw_params(): DMA adapter for 32-bit words, no padding */
	sw.w32(REG_DMA_ADAPTER_A(fe), (channels << 20) | (2u << 13) | (2u << 5));
	/* frame sync as even as possible, MCLK divider 1 */
	mca.w32(cl + REG_SYNCGEN_HI_PERIOD, bclkRatio / 2 - 1);
	mca.w32(cl + REG_SYNCGEN_LO_PERIOD, (bclkRatio + 1) / 2 - 1);
	mca.w32(cl + REG_MCLK_CONF, 1u << 8);

	/* mca_fe_enable_clocks() */
	mca.w32(cl + REG_SYNCGEN_MCLK_SEL, fe + 1);
	mca.mask32(cl + REG_SYNCGEN_STATUS, 0, SYNCGEN_STATUS_EN);
	mca.mask32(cl + REG_STATUS, 0, STATUS_MCLK_EN);
	mca.w32(cl + REG_INTMASK, 0);
	clocksOn = true;
	return 0;
}

void
SFMca::prepareDma(void)
{
	const uint32_t txo = fe * CLUSTER_STRIDE + CLUSTER_TX_OFF;

	tx.configure(4, channels, sramBlock);

	/* mca_fe_early_trigger(): reset the serialiser while it is detached from the sync */
	mca.mask32(txo + REG_TX_SERDES_CONF, SERDES_CONF_SYNC_SEL, 0);
	mca.mask32(txo + REG_TX_SERDES_CONF, SERDES_CONF_SYNC_SEL, 7u << 16);
	mca.mask32(txo + REG_SERDES_STATUS, SERDES_STATUS_EN | SERDES_STATUS_RST, SERDES_STATUS_RST);
	IODelay(50);
	if (mca.r32(txo + REG_SERDES_STATUS) & SERDES_STATUS_RST)
		LOG("serialiser still in reset: 0x%08x", mca.r32(txo + REG_SERDES_STATUS));
	mca.mask32(txo + REG_TX_SERDES_CONF, SERDES_CONF_SYNC_SEL, 0);
	mca.mask32(txo + REG_TX_SERDES_CONF, SERDES_CONF_SYNC_SEL, (fe + 1) << 16);
	IODelay(100);

	/* admac_start_current_tx(): empty rings */
	tx.resetRings();
}

void
SFMca::startDma(void)
{
	const uint32_t txo = fe * CLUSTER_STRIDE + CLUSTER_TX_OFF;

	tx.start();
	/* mca_fe_trigger() */
	mca.mask32(txo + REG_SERDES_STATUS, SERDES_STATUS_EN | SERDES_STATUS_RST, SERDES_STATUS_EN);
	running = true;
}

void
SFMca::stopDma(void)
{
	const uint32_t txo = fe * CLUSTER_STRIDE + CLUSTER_TX_OFF;

	if (!attached)
		return;
	mca.mask32(txo + REG_SERDES_STATUS, SERDES_STATUS_EN, 0);
	tx.stop();
	tx.resetRings();
	running = false;
}

void
SFMca::stopClocks(void)
{
	const uint32_t cl = fe * CLUSTER_STRIDE;

	if (!attached || !clocksOn)
		return;
	/* mca_be_shutdown(), mca_fe_disable_clocks() */
	for (unsigned p = 0; p < 6; p++) {
		const uint32_t pt = p * CLUSTER_STRIDE;
		if (!(ports & (1u << p)))
			continue;
		mca.mask32(pt + REG_PORT_ENABLES, PORT_ENABLES_TX_DATA, 0);
		mca.w32(pt + REG_PORT_DATA_SEL, 0);
		mca.mask32(pt + REG_PORT_ENABLES, PORT_ENABLES_CLOCKS, 0);
		mca.w32(pt + REG_PORT_CLOCK_SEL, 0);
	}
	mca.mask32(cl + REG_SYNCGEN_STATUS, SYNCGEN_STATUS_EN, 0);
	mca.mask32(cl + REG_STATUS, STATUS_MCLK_EN, 0);
	nco.w32(fe * NCO_CHANNEL_STRIDE + NCO_REG_CTRL, nco.r32(fe * NCO_CHANNEL_STRIDE + NCO_REG_CTRL) & ~NCO_CTRL_ENABLE);
	clocksOn = false;
}

void
SFMca::status(SFMcaStatus *out) const
{
	const uint32_t cl = fe * CLUSTER_STRIDE, txo = cl + CLUSTER_TX_OFF;
	uint32_t pt = 0;

	bzero(out, sizeof(*out));
	if (!attached)
		return;
	for (unsigned p = 0; p < 6; p++)
		if (ports & (1u << p)) {
			pt = p * CLUSTER_STRIDE;    /* the first port stands for all */
			break;
		}
	out->ncoCtrl = nco.r32(fe * NCO_CHANNEL_STRIDE + NCO_REG_CTRL);
	out->ncoDiv = nco.r32(fe * NCO_CHANNEL_STRIDE + NCO_REG_DIV);
	out->admacTxEnabled = admac.r32(REG_TX_START);
	out->descRing = admac.r32(REG_DESC_RING(tx.ch));
	out->reportRing = admac.r32(REG_REPORT_RING(tx.ch));
	out->chanStatus = admac.r32(REG_CHAN_INTSTATUS(tx.ch, ADMAC_IRQ_INDEX));
	out->residue = admac.r32(REG_RESIDUE(tx.ch));
	/* the cluster's registers answer only while its power domain has a clock */
	if (!clocksOn)
		return;
	out->mclkStatus = mca.r32(cl + REG_STATUS);
	out->syncgenStatus = mca.r32(cl + REG_SYNCGEN_STATUS);
	out->serdesStatus = mca.r32(txo + REG_SERDES_STATUS);
	out->serdesConf = mca.r32(txo + REG_TX_SERDES_CONF);
	out->portEnables = mca.r32(pt + REG_PORT_ENABLES);
	out->portClockSel = mca.r32(pt + REG_PORT_CLOCK_SEL);
	out->portDataSel = mca.r32(pt + REG_PORT_DATA_SEL);
	out->mcaIntState = mca.r32(cl + REG_INTSTATE);
}

/* ---- capture stream ---- */

int
SFMcaCapture::attach(unsigned captureCluster, unsigned portMask, bool clockConsumer, unsigned syncFromPort,
    unsigned slotsPerFrame, unsigned slotWidth, unsigned channelCount, unsigned sram)
{
	if (attached)
		return 0;
	if (captureCluster > 5 || portMask == 0 || (portMask >> 6) != 0 || syncFromPort > 5 || slotsPerFrame < 2 ||
	    slotsPerFrame > 16 || (slotWidth != 16 && slotWidth != 32) || channelCount < 2 || channelCount > slotsPerFrame ||
	    sram > 7)
		return EINVAL;
	cluster = captureCluster;
	ports = portMask;
	consumer = clockConsumer;
	syncPort = syncFromPort;
	slots = slotsPerFrame;
	slotBits = slotWidth;
	channels = channelCount;
	sramBlock = sram;
	if (audio_power() != 0)
		return ENXIO;
	if (!mca.mapNode("/arm-io/mca-switch", 0) || !sw.mapNode("/arm-io/mca-switch", 1) ||
	    !admac.mapNode("/arm-io/admac-sio", 0))
		return ENXIO;
	rx.admac = &admac;
	/* the cluster's second receiver, as Linux uses it for capture (USE_RXB_FOR_CAPTURE) */
	rx.ch = 4 * cluster + 3;
	attached = true;
	return 0;
}

int
SFMcaCapture::prepare(void)
{
	const uint32_t cl = cluster * CLUSTER_STRIDE, rxo = cl + CLUSTER_RXB_OFF;
	int ret;

	if (!attached)
		return ENXIO;
	/* the clock of a consumer cluster's power domain is the one the port brings: clock gated counts */
	ret = consumer ? sf_pmgr_enable_node("/arm-io/mca-switch", (int)cluster, kSFPmgrExternallyClocked, false) : 0;
	if (ret != 0)
		return ret;

	/* mca_fe_set_fmt(): I2S, inverted bit clock and frame, as the transmitter */
	mca.mask32(rxo + REG_RX_SERDES_CONF, SERDES_CONF_BCLK_POL, 0);
	mca.w32(rxo + REG_RX_SERDES_BITSTART, 1);
	/* mca_configure_serdes(): the first `channels` slots captured, from these ports */
	mca.mask32(rxo + REG_RX_SERDES_CONF,
	    SERDES_CONF_WIDTH_MASK | SERDES_CONF_NCHANS | SERDES_CONF_SYNC_SEL | SERDES_CONF_UNK1 | SERDES_CONF_UNK2 |
	    SERDES_CONF_UNK3 | SERDES_CONF_NO_DATA_FEEDBACK,
	    (slots - 1) | (slotBits == 16 ? SERDES_CONF_WIDTH_16BIT : SERDES_CONF_WIDTH_32BIT) | ((cluster + 1) << 16) |
	    SERDES_CONF_UNK1 | SERDES_CONF_UNK2 | SERDES_CONF_NO_DATA_FEEDBACK);
	mca.w32(rxo + REG_RX_SERDES_SLOTMASK, 0xffffffff);
	mca.w32(rxo + REG_RX_SERDES_SLOTMASK + 0x4, ~((1u << channels) - 1));
	mca.w32(rxo + REG_RX_SERDES_PORT, ports);
	/* mca_fe_hw_params(): samples in 32-bit DMA words, padded to 32 bits */
	sw.w32(REG_DMA_ADAPTER_B(cluster), ((channels > 4 ? 4u : channels) << 20) | (2u << 13) | ((32 - slotBits) << 8) |
	    (2u << 5) | (32 - slotBits));
	if (consumer) {
		mca.w32(cl + REG_SYNCGEN_HI_PERIOD, slots * slotBits / 2 - 1);
		mca.w32(cl + REG_SYNCGEN_LO_PERIOD, (slots * slotBits + 1) / 2 - 1);
		mca.w32(cl + REG_MCLK_CONF, 1u << 8);
		/* mca_fe_prepare() of a clock consumer: the sync generator runs from the port's clock */
		mca.w32(cl + REG_SYNCGEN_MCLK_SEL, syncPort + 6 + 1);
		mca.mask32(cl + REG_SYNCGEN_STATUS, 0, SYNCGEN_STATUS_EN);
	}

	rx.configure(slotBits / 8, channels > 4 ? 4 : channels, sramBlock);
	/* mca_fe_early_trigger() */
	mca.mask32(rxo + REG_RX_SERDES_CONF, SERDES_CONF_SYNC_SEL, 0);
	mca.mask32(rxo + REG_RX_SERDES_CONF, SERDES_CONF_SYNC_SEL, 7u << 16);
	mca.mask32(rxo + REG_SERDES_STATUS, SERDES_STATUS_EN | SERDES_STATUS_RST, SERDES_STATUS_RST);
	IODelay(50);
	mca.mask32(rxo + REG_RX_SERDES_CONF, SERDES_CONF_SYNC_SEL, 0);
	mca.mask32(rxo + REG_RX_SERDES_CONF, SERDES_CONF_SYNC_SEL, (cluster + 1) << 16);
	IODelay(100);
	rx.resetRings();
	return 0;
}

void
SFMcaCapture::start(void)
{
	const uint32_t rxo = cluster * CLUSTER_STRIDE + CLUSTER_RXB_OFF;

	rx.start();
	mca.mask32(rxo + REG_SERDES_STATUS, SERDES_STATUS_EN | SERDES_STATUS_RST, SERDES_STATUS_EN);
	running = true;
}

void
SFMcaCapture::stop(void)
{
	const uint32_t cl = cluster * CLUSTER_STRIDE, rxo = cl + CLUSTER_RXB_OFF;

	if (!attached || !running)
		return;
	mca.mask32(rxo + REG_SERDES_STATUS, SERDES_STATUS_EN, 0);
	rx.stop();
	rx.resetRings();
	/* mca_fe_hw_free() */
	if (consumer)
		mca.mask32(cl + REG_SYNCGEN_STATUS, SYNCGEN_STATUS_EN, 0);
	running = false;
}

void
SFMcaCapture::status(uint32_t out[3]) const
{
	const uint32_t cl = cluster * CLUSTER_STRIDE, rxo = cl + CLUSTER_RXB_OFF;

	out[0] = out[1] = out[2] = 0;
	if (!attached || !running)
		return;
	out[0] = mca.r32(rxo + REG_SERDES_STATUS);
	out[1] = mca.r32(rxo + REG_RX_SERDES_CONF);
	out[2] = mca.r32(cl + REG_SYNCGEN_STATUS);
}
