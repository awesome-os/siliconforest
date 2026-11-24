// SPDX-License-Identifier: GPL-2.0-only
/*
 * CS42L83 headphone codec, playback path.
 *
 * Ported from the Omacom Linux tree, sound/soc/codecs/cs42l42.c and cs42l83-i2c.c
 * (cs42l42_common_probe, cs42l42_init, cs42l42_setup_hs_type_detect,
 * cs42l42_set_dai_fmt, cs42l42_pcm_hw_params, cs42l42_pll_config, cs42l42_asp_config,
 * cs42l42_src_config, cs42l42_mute_stream, the DAPM playback widgets) and
 * include/sound/cs42l42.h:
 *   Copyright 2016-2022 Cirrus Logic, Inc.
 *   Authors: James Schulman, Brian Austin, Michael White (Cirrus Logic)
 *   cs42l83-i2c.c: Martin Povišer
 */
#include "sf_cs42l83.h"

#include <IOKit/IOLib.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFCs42l83: " fmt "\n", ##__VA_ARGS__)

#define CS42L42_PAGE_REGISTER           0x00
#define CS42L83_CHIP_ID                 0x42A83

#define CS42L42_DEVID_AB                0x1001
#define CS42L42_DEVID_CD                0x1002
#define CS42L42_DEVID_E                 0x1003
#define CS42L42_REVID                   0x1005
#define CS42L42_MCLK_CTL                0x1009
#define CS42L42_INTERNAL_FS_MASK        (1 << 1)
#define CS42L42_SFTRAMP_RATE            0x100A
#define CS42L42_PWR_CTL1                0x1101
#define CS42L42_ASP_DAO_PDN_MASK        (1 << 7)
#define CS42L42_ASP_DAI_PDN_MASK        (1 << 6)
#define CS42L42_MIXER_PDN_MASK          (1 << 5)
#define CS42L42_EQ_PDN_MASK             (1 << 4)
#define CS42L42_HP_PDN_MASK             (1 << 3)
#define CS42L42_ADC_PDN_MASK            (1 << 2)
#define CS42L42_PDN_ALL_MASK            (1 << 0)
#define CS42L42_OSC_SWITCH              0x1107
#define CS42L42_SCLK_PRESENT_MASK       (1 << 0)
#define CS42L42_OSC_SWITCH_STATUS       0x1109
#define CS42L42_TSENSE_CTL              0x1113
#define CS42L42_TS_RISE_DBNCE_TIME_MASK (7 << 0)
#define CS42L42_TS_FALL_DBNCE_TIME_MASK (7 << 3)
#define CS42L42_TS_INV_MASK             (1 << 7)
#define CS42L42_TS_DBNCE_1000           5
#define CS42L42_HS_CLAMP_DISABLE        0x1129
#define CS42L42_MCLK_SRC_SEL            0x1201
#define CS42L42_MCLK_SRC_SEL_MASK       (1 << 0)
#define CS42L42_FSYNC_PW_LOWER          0x1203
#define CS42L42_FSYNC_PW_UPPER          0x1204
#define CS42L42_FSYNC_P_LOWER           0x1205
#define CS42L42_FSYNC_P_UPPER           0x1206
#define CS42L42_ASP_CLK_CFG             0x1207
#define CS42L42_ASP_SCLK_EN_MASK        (1 << 5)
#define CS42L42_ASP_MODE_MASK           (1 << 4)
#define CS42L42_ASP_SCPOL_MASK          (3 << 2)
#define CS42L42_ASP_LCPOL_MASK          (3 << 0)
#define CS42L42_ASP_LCPOL_INV           3
#define CS42L42_ASP_FRM_CFG             0x1208
#define CS42L42_ASP_STP_MASK            (1 << 4)
#define CS42L42_ASP_5050_MASK           (1 << 3)
#define CS42L42_ASP_FSD_MASK            (7 << 0)
#define CS42L42_ASP_FSD_1_0             2
#define CS42L42_FS_RATE_EN              0x1209
#define CS42L42_FS_EN_MASK              0x0f
#define CS42L42_FS_EN_IASRC_96K         0x1
#define CS42L42_FS_EN_OASRC_96K         0x2
#define CS42L42_IN_ASRC_CLK             0x120A
#define CS42L42_OUT_ASRC_CLK            0x120B
#define CS42L42_PLL_DIV_CFG1            0x120C
#define CS42L42_SCLK_PREDIV_MASK        (3 << 0)
#define CS42L42_SRC_STATUS              0x1303
#define CS42L42_SRC_ILK_MASK            (1 << 0)
#define CS42L42_ASP_RX_STATUS           0x1304
#define CS42L42_CODEC_STATUS            0x1308
#define CS42L42_PLL_LOCK_STATUS         0x130E
#define CS42L42_TSRS_PLUG_STATUS        0x130F
#define CS42L42_TS_PLUG_SHIFT           2
#define CS42L42_PLL_CTL1                0x1501
#define CS42L42_PLL_START_MASK          (1 << 0)
#define CS42L42_PLL_DIV_FRAC0           0x1502
#define CS42L42_PLL_DIV_FRAC1           0x1503
#define CS42L42_PLL_DIV_FRAC2           0x1504
#define CS42L42_PLL_DIV_INT             0x1505
#define CS42L42_PLL_CTL3                0x1508
#define CS42L42_PLL_CAL_RATIO           0x150A
#define CS42L42_PLL_CTL4                0x151B
#define CS42L42_PLL_MODE_MASK           (3 << 0)
#define CS42L42_TIPSENSE_CTL            0x1B73
#define CS42L42_TIP_SENSE_DEBOUNCE_MASK (3 << 0)
#define CS42L42_TIP_SENSE_INV_MASK      (1 << 5)
#define CS42L42_TIP_SENSE_CTRL_MASK     (3 << 6)
#define CS42L42_MISC_DET_CTL            0x1B74
#define CS42L42_DETECT_MODE_MASK        (3 << 3)
#define CS42L42_MIC_DET_CTL1            0x1B75
#define CS42L42_HS_DET_LEVEL_MASK       0x3f
#define CS42L42_EVENT_STAT_SEL_MASK     (1 << 6)
#define CS42L42_LATCH_TO_VP_MASK        (1 << 7)
#define CS42L42_HS_DET_LEVEL_15         0x0F
#define CS42L42_DET_STATUS1             0x1B77
#define CS42L42_HS_BIAS_CTL             0x1C03
#define CS42L42_HSBIAS_RAMP_MASK        (3 << 0)
#define CS42L42_HSBIAS_RAMP_SLOW        2
#define CS42L42_HP_CTL                  0x2001
#define CS42L42_HP_ANA_BMUTE_MASK       (1 << 3)
#define CS42L42_HP_ANA_AMUTE_MASK       (1 << 2)
#define CS42L42_MIXER_CHA_VOL           0x2301
#define CS42L42_MIXER_CHB_VOL           0x2303
#define CS42L42_MIXER_CH_VOL_MASK       0x3f
#define CS42L42_SP_RX_CH_SEL            0x2501
#define CS42L42_SP_RX_CHB_SEL_MASK      (3 << 2)
#define CS42L42_ASP_TX_SZ_EN            0x2901
#define CS42L42_ASP_TX_CH_EN            0x2902
#define CS42L42_ASP_TX_CH_AP_RES        0x2903
#define CS42L42_ASP_TX_CH1_BIT_MSB      0x2904
#define CS42L42_ASP_TX_CH1_BIT_LSB      0x2905
#define CS42L42_ASP_TX_CH2_BIT_MSB      0x290A
#define CS42L42_ASP_TX_CH2_BIT_LSB      0x290B
#define CS42L42_ASP_RX_DAI0_EN          0x2A01
#define CS42L42_ASP_RX0_CH_EN_MASK      (0xf << 2)
#define CS42L42_ASP_RX_DAI0_CH1_AP_RES  0x2A02
#define CS42L42_ASP_RX_DAI0_CH1_BIT_MSB 0x2A03
#define CS42L42_ASP_RX_DAI0_CH1_BIT_LSB 0x2A04
#define CS42L42_ASP_RX_DAI0_CH2_AP_RES  0x2A05
#define CS42L42_ASP_RX_DAI0_CH2_BIT_MSB 0x2A06
#define CS42L42_ASP_RX_DAI0_CH2_BIT_LSB 0x2A07
#define CS42L42_ASP_RX_CH_AP_MASK       (1 << 6)
#define CS42L42_ASP_RX_CH_RES_MASK      (3 << 0)
#define CS42L42_ASP_RX_CH_RES_32        3

/* jack detection */
#define CS42L42_HSDET_CTL1              0x111F
#define CS42L42_HSDET_COMP1_LVL_VAL     12      /* 1.25 V */
#define CS42L42_HSDET_COMP2_LVL_VAL     2       /* 1.75 V */
#define CS42L42_HSDET_COMP1_LVL_DEFAULT 7
#define CS42L42_HSDET_COMP2_LVL_DEFAULT 7
#define CS42L42_HSDET_CTL2              0x1120
#define CS42L42_HSDET_AUTO_TIME_MASK    (3 << 0)
#define CS42L42_HSBIAS_REF_MASK         (1 << 3)
#define CS42L42_HSDET_SET_MASK          (3 << 4)
#define CS42L42_HSDET_CTRL_MASK         (3 << 6)
#define CS42L42_HSDET_CTL2_ALL          0xfb
#define CS42L42_HS_SWITCH_CTL           0x1121
#define CS42L42_HSDET_SW_COMP1          0xa6
#define CS42L42_HSDET_SW_COMP2          0x59
#define CS42L42_HSDET_SW_TYPE1          0xa6
#define CS42L42_HSDET_SW_TYPE2          0x59
#define CS42L42_HSDET_SW_TYPE3          0xf3
#define CS42L42_HS_DET_STATUS           0x1124
#define CS42L42_HSDET_TYPE_MASK         (3 << 0)
#define CS42L42_HSDET_COMP1_OUT_SHIFT   6
#define CS42L42_HSDET_COMP2_OUT_SHIFT   7
#define CS42L42_PLUG_CTIA               0
#define CS42L42_PLUG_OMTP               1
#define CS42L42_PLUG_HEADPHONE          2
#define CS42L42_PLUG_INVALID            3
#define CS42L42_HSDET_COMP_TYPE1        1
#define CS42L42_HSDET_COMP_TYPE2        2
#define CS42L42_ADC_OVFL_STATUS         0x1301
#define CS42L42_MIXER_STATUS            0x1302
#define CS42L42_ASP_TX_STATUS           0x1305
#define CS42L42_DET_INT_STATUS1         0x1309
#define CS42L42_DET_INT_STATUS2         0x130A
#define CS42L42_SRCPL_INT_STATUS        0x130B
#define CS42L42_VPMON_STATUS            0x130D
#define CS42L42_ADC_OVFL_INT_MASK       0x1316
#define CS42L42_MIXER_INT_MASK          0x1317
#define CS42L42_SRC_INT_MASK            0x1318
#define CS42L42_ASP_RX_INT_MASK         0x1319
#define CS42L42_ASP_TX_INT_MASK         0x131A
#define CS42L42_CODEC_INT_MASK          0x131B
#define CS42L42_PDN_DONE_MASK           (1 << 0)
#define CS42L42_HSDET_AUTO_DONE_MASK    (1 << 1)
#define CS42L42_SRCPL_INT_MASK          0x131C
#define CS42L42_VPMON_INT_MASK          0x131E
#define CS42L42_PLL_LOCK_INT_MASK       0x131F
#define CS42L42_TSRS_PLUG_INT_MASK      0x1320
#define CS42L42_RS_PLUG_MASK            (1 << 0)
#define CS42L42_RS_UNPLUG_MASK          (1 << 1)
#define CS42L42_TS_PLUG_MASK            (1 << 2)
#define CS42L42_TS_UNPLUG_MASK          (1 << 3)
#define CS42L42_TS_PLUG                 3
#define CS42L42_TS_UNPLUG               0
#define CS42L42_TS_TRANS                1
#define CS42L42_HSBIAS_SC_AUTOCTL       0x1B70
#define CS42L42_HSBIAS_SENSE_TRIP_MASK  (7 << 0)
#define CS42L42_TIP_SENSE_EN_MASK       (1 << 5)
#define CS42L42_AUTO_HSBIAS_HIZ_MASK    (1 << 6)
#define CS42L42_HSBIAS_SENSE_EN_MASK    (1 << 7)
#define CS42L42_WAKE_CTL                0x1B71
#define CS42L42_M_HP_WAKE_MASK          (1 << 6)
#define CS42L42_M_MIC_WAKE_MASK         (1 << 7)
#define CS42L42_PDN_MIC_LVL_DET_MASK    (1 << 0)
#define CS42L42_HSBIAS_CTL_MASK         (3 << 1)
#define CS42L42_MIC_DET_CTL2            0x1B76
#define CS42L42_DET_STATUS2             0x1B78
#define CS42L42_SHORT_TRUE_MASK         (1 << 0)
#define CS42L42_HS_TRUE_MASK            (1 << 1)
#define CS42L42_DET_INT1_MASK           0x1B79
#define CS42L42_TIP_SENSE_UNPLUG_MASK   (1 << 5)
#define CS42L42_TIP_SENSE_PLUG_MASK     (1 << 6)
#define CS42L42_HSBIAS_SENSE_MASK       (1 << 7)
#define CS42L42_DET_INT2_MASK           0x1B7A
#define CS42L42_M_SHORT_DET_MASK        (1 << 0)
#define CS42L42_M_SHORT_RLS_MASK        (1 << 1)
#define CS42L42_M_HSBIAS_HIZ_MASK       (1 << 2)
#define CS42L42_M_DETECT_FT_MASK        (1 << 6)
#define CS42L42_M_DETECT_TF_MASK        (1 << 7)
#define CS42L42_DET_INT_VAL2_MASK       0xc7
#define CS42L42_DAC_CTL2                0x1F06
#define CS42L42_DAC_CTL2_ALL            0xff    /* pull-down, load, clamp, HPF, monitor */

/* device-tree defaults of cs42l42_handle_device_data() */
#define BTN_DET_INIT_DBNCE_MS           100
#define BTN_DET_EVENT_DBNCE_MS          10
#define HS_BIAS_RAMP_TIME_MS            90
#define HS_BIAS_SENSE_EN                1
static const uint8_t kBiasThresholds[4] = { 0x0F, 0x08, 0x04, 0x01 };

/* the Apple inline remote of the CS42L83 (see cs42l83_remote_start) */
#define CS42L83_REMOTE_TX_START         (1 << 7)
#define CS42L83_HSBIAS_CTL_ON           (2 << 1)
#define CS42L83_HS_BIAS_CTL_TRANSITION  (3 << 2)
#define CS42L83_REMOTE_BUTTONS          0x1f
#define CS42L83_REMOTE_VOLUME_DOWN      (1 << 0)
#define CS42L83_REMOTE_VOLUME_UP        (1 << 1)
#define CS42L83_REMOTE_RX_ACK           (1 << 3)
#define CS42L83_REMOTE_BUTTON_IRQ       (1 << 5)
#define CS42L83_REMOTE_BIAS_RESET       0x03
#define CS42L83_REMOTE_BIAS_IDENTIFY    0x9e
#define CS42L83_REMOTE_BIAS_BUTTONS     0x5f
#define CS42L83_REMOTE_RX_CONFIG        0x8f
#define CS42L83_REMOTE_LEVEL_IDENTIFY   0x36
#define CS42L83_REMOTE_LEVEL_BUTTONS    0x3c
#define CS42L83_REMOTE_SENSE_TRIP       3
#define CS42L83_REMOTE_SETTLE_MS        35
enum { kSavedAutoctl, kSavedWake, kSavedMiscDet, kSavedMicDet1, kSavedMicDet2, kSavedHsBias, kSavedHsdet2,
       kSavedInt1Mask, kSavedInt2Mask, kSavedCount };

#define CS42L42_BOOT_TIME_US            3000
#define CS42L42_PLL_DIVOUT_TIME_US      800
#define CS42L42_CLOCK_SWITCH_DELAY_US   150
#define CS42L42_HP_ADC_EN_TIME_US       20000

/* The two rows of pll_ratio_table (datasheet table 4-5) for 64 bit clocks per frame. */
static const struct {
	uint32_t sclk;
	uint8_t  sclk_prediv, pll_div_int;
	uint32_t pll_div_frac;
	uint8_t  pll_mode, pll_divout;
	uint32_t mclk_int;
	uint8_t  pll_cal_ratio, n;
} kPll[] = {
	{ 2822400, 0x00, 0x40, 0x000000, 0x03, 0x10, 11289600, 128, 1 },   /* 44.1 kHz */
	{ 3072000, 0x00, 0x3E, 0x800000, 0x03, 0x10, 12000000, 125, 1 },   /* 48 kHz */
};

/* regLock held */
static int
select_page(SFCs42l83 *c, uint16_t reg)
{
	int err = 0;
	if (c->page != (reg >> 8)) {
		err = c->bus->writeReg(c->addr, CS42L42_PAGE_REGISTER, (uint8_t)(reg >> 8));
		c->page = err ? -1 : (reg >> 8);
	}
	return err;
}

int
SFCs42l83::read(uint16_t reg, uint8_t *value)
{
	IOLockLock(regLock);
	int err = select_page(this, reg);
	if (err == 0)
		err = bus->readReg(addr, (uint8_t)reg, value);
	IOLockUnlock(regLock);
	return err;
}

int
SFCs42l83::write(uint16_t reg, uint8_t value)
{
	IOLockLock(regLock);
	int err = select_page(this, reg);
	if (err == 0)
		err = bus->writeReg(addr, (uint8_t)reg, value);
	IOLockUnlock(regLock);
	return err;
}

int
SFCs42l83::update(uint16_t reg, uint8_t mask, uint8_t value)
{
	uint8_t old = 0;

	IOLockLock(regLock);
	int err = select_page(this, reg);
	if (err == 0)
		err = bus->readReg(addr, (uint8_t)reg, &old);
	uint8_t now = (uint8_t)((old & ~mask) | (value & mask));
	if (err == 0 && now != old)
		err = bus->writeReg(addr, (uint8_t)reg, now);
	IOLockUnlock(regLock);
	return err;
}

int
SFCs42l83::probe(SFI2C *i2c, uint8_t address, SFGpio *resetGpio, unsigned resetPin)
{
	uint8_t ab = 0, cd = 0, e = 0;
	int err;

	if (regLock == NULL)
		regLock = IOLockAlloc();
	if (jackLock == NULL)
		jackLock = IOLockAlloc();
	if (regLock == NULL || jackLock == NULL)
		return ENOMEM;
	bus = i2c;
	addr = address;
	page = -1;
	ready = false;
	onSclk = false;

	/* cs42l42_common_probe(): reset low for the minimum pulse, then the boot time */
	resetGpio->output(resetPin, 0);
	IOSleep(1);
	resetGpio->output(resetPin, 1);
	IOSleep(2 * CS42L42_BOOT_TIME_US / 1000);

	/* cirrus_read_device_id() */
	err = read(CS42L42_DEVID_AB, &ab);
	if (err == 0)
		err = read(CS42L42_DEVID_CD, &cd);
	if (err == 0)
		err = read(CS42L42_DEVID_E, &e);
	if (err != 0) {
		LOG("no answer at 0x%02x (error %d)", addr, err);
		return err;
	}
	devid = ((uint32_t)ab << 12) | ((uint32_t)cd << 4) | (e >> 4);
	err = read(CS42L42_REVID, &revid);
	if (err != 0)
		return err;
	if (devid != CS42L83_CHIP_ID) {
		LOG("device ID 0x%05x, expected 0x%05x", devid, CS42L83_CHIP_ID);
		return ENODEV;
	}

	/* cs42l42_init(): leave global power-down, every block still off */
	err = update(CS42L42_PWR_CTL1,
	    CS42L42_ASP_DAO_PDN_MASK | CS42L42_ASP_DAI_PDN_MASK | CS42L42_MIXER_PDN_MASK | CS42L42_EQ_PDN_MASK |
	    CS42L42_HP_PDN_MASK | CS42L42_ADC_PDN_MASK | CS42L42_PDN_ALL_MASK,
	    CS42L42_ASP_DAO_PDN_MASK | CS42L42_ASP_DAI_PDN_MASK | CS42L42_MIXER_PDN_MASK | CS42L42_EQ_PDN_MASK |
	    CS42L42_HP_PDN_MASK | CS42L42_ADC_PDN_MASK);
	/* a faster digital ramp, so streams do not fade in audibly */
	if (err == 0)
		err = write(CS42L42_SFTRAMP_RATE, (10 << 4) | 1);
	/* cs42l42_handle_device_data() defaults: tip sense debounce 1 s up, none down; slow bias ramp */
	if (err == 0)
		err = update(CS42L42_TSENSE_CTL, CS42L42_TS_RISE_DBNCE_TIME_MASK | CS42L42_TS_FALL_DBNCE_TIME_MASK,
		    CS42L42_TS_DBNCE_1000);
	if (err == 0)
		err = update(CS42L42_HS_BIAS_CTL, CS42L42_HSBIAS_RAMP_MASK, CS42L42_HSBIAS_RAMP_SLOW);
	/* cs42l42_setup_hs_type_detect() */
	if (err == 0)
		err = update(CS42L42_MISC_DET_CTL, CS42L42_DETECT_MODE_MASK, 0);
	if (err == 0)
		err = update(CS42L42_MIC_DET_CTL1,
		    CS42L42_LATCH_TO_VP_MASK | CS42L42_EVENT_STAT_SEL_MASK | CS42L42_HS_DET_LEVEL_MASK,
		    CS42L42_LATCH_TO_VP_MASK | CS42L42_HS_DET_LEVEL_15);
	if (err == 0)
		err = update(CS42L42_HS_CLAMP_DISABLE, 1, 1);
	/* tip sense on; the J293 jack switch is inverted (cirrus,ts-inv = 1) */
	if (err == 0)
		err = update(CS42L42_TSENSE_CTL, CS42L42_TS_INV_MASK, CS42L42_TS_INV_MASK);
	if (err == 0)
		err = update(CS42L42_TIPSENSE_CTL,
		    CS42L42_TIP_SENSE_CTRL_MASK | CS42L42_TIP_SENSE_INV_MASK | CS42L42_TIP_SENSE_DEBOUNCE_MASK,
		    (3 << 6) | (0 << 5) | 2);
	/* the interrupt masks stay at their reset values (all masked) until jackStart() */
	if (err != 0) {
		LOG("initialisation failed (error %d)", err);
		return err;
	}
	ready = true;
	return 0;
}

int
SFCs42l83::configure(uint32_t rate)
{
	unsigned i = rate == 44100 ? 0 : 1;
	const uint32_t fsync = 64;
	int err;

	if (!ready)
		return ENXIO;
	if (onSclk)
		return EBUSY;
	if (rate != 44100 && rate != 48000)
		return EINVAL;
	pll = i;

	/* cs42l42_set_dai_fmt(): clock consumer, I2S, inverted bit clock and frame */
	err = update(CS42L42_ASP_FRM_CFG, CS42L42_ASP_STP_MASK | CS42L42_ASP_5050_MASK | CS42L42_ASP_FSD_MASK,
	    CS42L42_ASP_5050_MASK | CS42L42_ASP_FSD_1_0);
	if (err == 0)
		err = update(CS42L42_ASP_CLK_CFG, CS42L42_ASP_MODE_MASK | CS42L42_ASP_SCPOL_MASK | CS42L42_ASP_LCPOL_MASK,
		    CS42L42_ASP_LCPOL_INV);

	/* cs42l42_pcm_hw_params(), playback: 32-bit samples, channel 1 on low LRCLK, 2 on high */
	if (err == 0)
		err = update(CS42L42_ASP_RX_DAI0_CH1_AP_RES, CS42L42_ASP_RX_CH_AP_MASK | CS42L42_ASP_RX_CH_RES_MASK,
		    CS42L42_ASP_RX_CH_RES_32);
	if (err == 0)
		err = update(CS42L42_ASP_RX_DAI0_CH2_AP_RES, CS42L42_ASP_RX_CH_AP_MASK | CS42L42_ASP_RX_CH_RES_MASK,
		    CS42L42_ASP_RX_CH_AP_MASK | CS42L42_ASP_RX_CH_RES_32);
	if (err == 0)
		err = write(CS42L42_ASP_RX_DAI0_CH1_BIT_MSB, 0);
	if (err == 0)
		err = write(CS42L42_ASP_RX_DAI0_CH1_BIT_LSB, 0);
	if (err == 0)
		err = write(CS42L42_ASP_RX_DAI0_CH2_BIT_MSB, 0);
	if (err == 0)
		err = write(CS42L42_ASP_RX_DAI0_CH2_BIT_LSB, 0);
	/* channel B from the second channel; both LRCLK slots enabled */
	if (err == 0)
		err = update(CS42L42_SP_RX_CH_SEL, CS42L42_SP_RX_CHB_SEL_MASK, 1 << 2);
	if (err == 0)
		err = update(CS42L42_ASP_RX_DAI0_EN, CS42L42_ASP_RX0_CH_EN_MASK, (1 << 2) | (1 << 3));

	/* cs42l42_pll_config() */
	if (err == 0)
		err = update(CS42L42_MCLK_CTL, CS42L42_INTERNAL_FS_MASK,
		    kPll[i].mclk_int != 12000000 ? CS42L42_INTERNAL_FS_MASK : 0);
	if (err == 0)
		err = update(CS42L42_PLL_DIV_CFG1, CS42L42_SCLK_PREDIV_MASK, kPll[i].sclk_prediv);
	if (err == 0)
		err = write(CS42L42_PLL_DIV_INT, kPll[i].pll_div_int);
	if (err == 0)
		err = write(CS42L42_PLL_DIV_FRAC0, (uint8_t)kPll[i].pll_div_frac);
	if (err == 0)
		err = write(CS42L42_PLL_DIV_FRAC1, (uint8_t)(kPll[i].pll_div_frac >> 8));
	if (err == 0)
		err = write(CS42L42_PLL_DIV_FRAC2, (uint8_t)(kPll[i].pll_div_frac >> 16));
	if (err == 0)
		err = update(CS42L42_PLL_CTL4, CS42L42_PLL_MODE_MASK, kPll[i].pll_mode);
	if (err == 0)
		err = write(CS42L42_PLL_CTL3, (uint8_t)(kPll[i].pll_divout * kPll[i].n));
	if (err == 0)
		err = write(CS42L42_PLL_CAL_RATIO, kPll[i].pll_cal_ratio);

	/* cs42l42_asp_config(): LRCLK period and 50 % duty cycle */
	if (err == 0)
		err = write(CS42L42_FSYNC_P_LOWER, (uint8_t)(fsync - 1));
	if (err == 0)
		err = write(CS42L42_FSYNC_P_UPPER, 0);
	if (err == 0)
		err = write(CS42L42_FSYNC_PW_LOWER, (uint8_t)(fsync / 2 - 1));
	if (err == 0)
		err = write(CS42L42_FSYNC_PW_UPPER, 0);

	/* cs42l42_src_config(): rates of 96 kHz or lower, converter clock for up to 48 kHz */
	if (err == 0)
		err = update(CS42L42_FS_RATE_EN, CS42L42_FS_EN_MASK, CS42L42_FS_EN_IASRC_96K | CS42L42_FS_EN_OASRC_96K);
	if (err == 0)
		err = update(CS42L42_IN_ASRC_CLK, 1, 0);
	if (err == 0)
		err = update(CS42L42_OUT_ASRC_CLK, 1, 0);
	if (err != 0)
		LOG("configuration for %u Hz failed (error %d)", rate, err);
	return err;
}

int
SFCs42l83::setVolume(unsigned attenuation)
{
	uint8_t v = attenuation > 0x3f ? 0x3f : (uint8_t)attenuation;
	int err;

	if (!ready)
		return ENXIO;
	err = update(CS42L42_MIXER_CHA_VOL, CS42L42_MIXER_CH_VOL_MASK, v);
	if (err == 0)
		err = update(CS42L42_MIXER_CHB_VOL, CS42L42_MIXER_CH_VOL_MASK, v);
	return err;
}

int
SFCs42l83::mute(bool on)
{
	if (!ready)
		return ENXIO;
	return update(CS42L42_HP_CTL, CS42L42_HP_ANA_AMUTE_MASK | CS42L42_HP_ANA_BMUTE_MASK,
	    on ? (CS42L42_HP_ANA_AMUTE_MASK | CS42L42_HP_ANA_BMUTE_MASK) : 0);
}

int
SFCs42l83::powerUp(unsigned attenuation)
{
	uint8_t lock = 0;
	int err;

	if (!ready)
		return ENXIO;
	err = mute(true);
	if (err == 0)
		err = setVolume(attenuation);
	/* the DAPM playback widgets in power-up order: supplies, mixer, DAC */
	if (err == 0)
		err = update(CS42L42_ASP_CLK_CFG, CS42L42_ASP_SCLK_EN_MASK, CS42L42_ASP_SCLK_EN_MASK);
	if (err == 0)
		err = update(CS42L42_PWR_CTL1, CS42L42_ASP_DAI_PDN_MASK, 0);
	if (err == 0)
		err = update(CS42L42_PWR_CTL1, CS42L42_MIXER_PDN_MASK, 0);
	if (err == 0)
		err = update(CS42L42_PWR_CTL1, CS42L42_HP_PDN_MASK, 0);
	if (err != 0)
		return err;
	IOSleep(CS42L42_HP_ADC_EN_TIME_US / 1000 + 1);

	/*
	 * cs42l42_mute_stream(unmute), without the unmute: start the PLL from the bit
	 * clock, wait for lock, and only then leave the oscillator. Without a locked PLL
	 * the chip would have no clock and its I2C interface would stop answering.
	 */
	err = update(CS42L42_PLL_CTL1, CS42L42_PLL_START_MASK, 1);
	if (err == 0 && kPll[pll].n > 1) {
		IODelay(CS42L42_PLL_DIVOUT_TIME_US);
		err = write(CS42L42_PLL_CTL3, kPll[pll].pll_divout);
	}
	for (int i = 0; err == 0 && i < 20; i++) {
		err = read(CS42L42_PLL_LOCK_STATUS, &lock);
		if (lock & 1)
			break;
		IOSleep(1);
	}
	if (err == 0 && !(lock & 1)) {
		LOG("the PLL did not lock (status 0x%02x); staying on the oscillator", lock);
		update(CS42L42_PLL_CTL1, CS42L42_PLL_START_MASK, 0);
		return ETIMEDOUT;
	}
	if (err == 0)
		err = update(CS42L42_MCLK_SRC_SEL, CS42L42_MCLK_SRC_SEL_MASK, CS42L42_MCLK_SRC_SEL_MASK);
	if (err == 0)
		err = write(CS42L42_OSC_SWITCH, CS42L42_SCLK_PRESENT_MASK);
	IODelay(CS42L42_CLOCK_SWITCH_DELAY_US);
	if (err == 0)
		onSclk = true;
	return err;
}

int
SFCs42l83::powerDown(void)
{
	int err;

	if (!ready)
		return ENXIO;
	err = mute(true);
	if (onSclk) {
		/* cs42l42_mute_stream(mute): oscillator first, then disconnect and stop the PLL */
		int e = write(CS42L42_OSC_SWITCH, 0);
		IODelay(CS42L42_CLOCK_SWITCH_DELAY_US);
		if (e == 0)
			e = update(CS42L42_MCLK_SRC_SEL, CS42L42_MCLK_SRC_SEL_MASK, 0);
		IODelay(200);
		if (e == 0)
			e = update(CS42L42_PLL_CTL1, CS42L42_PLL_START_MASK, 0);
		if (e == 0)
			onSclk = false;
		else if (err == 0)
			err = e;
	}
	capture(false);
	/* the DAPM widgets in power-down order */
	update(CS42L42_PWR_CTL1, CS42L42_HP_PDN_MASK, CS42L42_HP_PDN_MASK);
	update(CS42L42_PWR_CTL1, CS42L42_MIXER_PDN_MASK, CS42L42_MIXER_PDN_MASK);
	update(CS42L42_PWR_CTL1, CS42L42_ASP_DAI_PDN_MASK, CS42L42_ASP_DAI_PDN_MASK);
	update(CS42L42_ASP_CLK_CFG, CS42L42_ASP_SCLK_EN_MASK, 0);
	return err;
}

int
SFCs42l83::capture(bool on)
{
	int err;

	if (!ready)
		return ENXIO;
	if (!on) {
		/* the DAPM capture widgets in power-down order */
		err = update(CS42L42_ASP_TX_CH_EN, 0x03, 0);
		update(CS42L42_PWR_CTL1, CS42L42_ADC_PDN_MASK, CS42L42_ADC_PDN_MASK);
		update(CS42L42_ASP_TX_SZ_EN, 0x01, 0);
		update(CS42L42_PWR_CTL1, CS42L42_ASP_DAO_PDN_MASK, CS42L42_ASP_DAO_PDN_MASK);
		return err;
	}
	if (!onSclk)
		return EBUSY;
	/* cs42l42_pcm_hw_params(), capture: 32-bit samples, channel 1 on low LRCLK, 2 on high */
	err = update(CS42L42_ASP_TX_CH_AP_RES, 0xcf, (1 << 6) | (3 << 2) | 3);
	if (err == 0)
		err = write(CS42L42_ASP_TX_CH1_BIT_MSB, 0);
	if (err == 0)
		err = write(CS42L42_ASP_TX_CH1_BIT_LSB, 0);
	if (err == 0)
		err = write(CS42L42_ASP_TX_CH2_BIT_MSB, 0);
	if (err == 0)
		err = write(CS42L42_ASP_TX_CH2_BIT_LSB, 0);
	/* the DAPM capture widgets in power-up order: supplies, converter, outputs */
	if (err == 0)
		err = update(CS42L42_PWR_CTL1, CS42L42_ASP_DAO_PDN_MASK, 0);
	if (err == 0)
		err = update(CS42L42_ASP_TX_SZ_EN, 0x01, 0x01);
	if (err == 0)
		err = update(CS42L42_PWR_CTL1, CS42L42_ADC_PDN_MASK, 0);
	IOSleep(CS42L42_HP_ADC_EN_TIME_US / 1000 + 1);
	if (err == 0)
		err = update(CS42L42_ASP_TX_CH_EN, 0x03, 0x03);
	return err;
}

bool
SFCs42l83::inputLocked(void)
{
	uint8_t v = 0;
	return ready && read(CS42L42_SRC_STATUS, &v) == 0 && (v & CS42L42_SRC_ILK_MASK);
}

void
SFCs42l83::status(SFCs42l83Status *out)
{
	static const uint16_t regs[] = {
		/* grouped by page: each change of page is one more transfer */
		/* not the sticky registers the jack path lives on (codec, detect, plug status) */
		CS42L42_PLL_LOCK_STATUS, CS42L42_ASP_RX_STATUS, CS42L42_SRC_STATUS,
		CS42L42_DET_STATUS1, CS42L42_OSC_SWITCH_STATUS, CS42L42_PWR_CTL1,
		CS42L42_HP_CTL, CS42L42_MIXER_CHA_VOL, CS42L42_MIXER_CHB_VOL, CS42L42_ASP_CLK_CFG,
		CS42L42_MCLK_SRC_SEL, CS42L42_PLL_CTL1,
	};
	uint8_t v[sizeof(regs) / sizeof(regs[0])];

	bzero(out, sizeof(*out));
	bzero(v, sizeof(v));
	if (!ready) {
		out->error = ENXIO;
		return;
	}
	for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
		int err = read(regs[i], &v[i]);
		if (err != 0 && out->error == 0)
			out->error = err;
	}
	out->pllLock = v[0];
	out->aspRx = v[1];
	out->src = v[2];
	out->det1 = v[3];
	out->oscSwitch = v[4];
	out->pwr1 = v[5];
	out->hpCtl = v[6];
	out->volA = v[7];
	out->volB = v[8];
	out->aspClk = v[9];
	out->mclkSrc = v[10];
	out->pllCtl1 = v[11];
}

/* ---- jack: plug, type, buttons -------------------------------------------------------- */

static const uint16_t kRemoteRegs[kSavedCount] = {
	CS42L42_HSBIAS_SC_AUTOCTL, CS42L42_WAKE_CTL, CS42L42_MISC_DET_CTL, CS42L42_MIC_DET_CTL1, CS42L42_MIC_DET_CTL2,
	CS42L42_HS_BIAS_CTL, CS42L42_HSDET_CTL2, CS42L42_DET_INT1_MASK, CS42L42_DET_INT2_MASK,
};

/* snd_soc_jack_report(): newButtons replaces the buttons in buttonMask */
void
SFCs42l83::report(uint8_t newJack, uint8_t newButtons, uint8_t buttonMask)
{
	uint8_t b = (uint8_t)((buttons & ~buttonMask) | (newButtons & buttonMask));
	uint8_t pressed = (uint8_t)(b & ~buttons);

	if (newJack == jack && b == buttons)
		return;
	if (pressed)
		buttonEvents++;
	jack = newJack;
	buttons = b;
	if (changed != NULL)
		changed(this, pressed);
}

/* cs42l42_manual_hs_type_detect() */
void
SFCs42l83::manualTypeDetect(void)
{
	uint8_t st = 0, comp1, comp2, sw;

	/* manual, active */
	update(CS42L42_HSDET_CTL2, CS42L42_HSDET_CTL2_ALL, 1 << 6);
	write(CS42L42_HSDET_CTL1, (CS42L42_HSDET_COMP2_LVL_VAL << 4) | CS42L42_HSDET_COMP1_LVL_VAL);
	/* a type 1 headset: SW_HSB_HS3 open, SW_HSB_HS4 closed */
	write(CS42L42_HS_SWITCH_CTL, CS42L42_HSDET_SW_COMP1);
	IOSleep(100);
	read(CS42L42_HS_DET_STATUS, &st);
	comp1 = (st >> CS42L42_HSDET_COMP1_OUT_SHIFT) & 1;
	comp2 = (st >> CS42L42_HSDET_COMP2_OUT_SHIFT) & 1;
	/* a type 2 headset: SW_HSB_HS3 closed */
	write(CS42L42_HS_SWITCH_CTL, CS42L42_HSDET_SW_COMP2);
	IOSleep(100);
	read(CS42L42_HS_DET_STATUS, &st);
	comp1 |= (uint8_t)(((st >> CS42L42_HSDET_COMP1_OUT_SHIFT) & 1) << 1);
	comp2 |= (uint8_t)(((st >> CS42L42_HSDET_COMP2_OUT_SHIFT) & 1) << 1);

	/* comparator 1 (1.25 V) decides, comparator 2 (1.75 V) is the fallback */
	if (comp1 == CS42L42_HSDET_COMP_TYPE1 || (comp1 != CS42L42_HSDET_COMP_TYPE2 && comp2 == CS42L42_HSDET_COMP_TYPE1)) {
		hsType = CS42L42_PLUG_CTIA;
		sw = CS42L42_HSDET_SW_TYPE1;
	} else if (comp1 == CS42L42_HSDET_COMP_TYPE2 || comp2 == CS42L42_HSDET_COMP_TYPE2) {
		hsType = CS42L42_PLUG_OMTP;
		sw = CS42L42_HSDET_SW_TYPE2;
	} else {
		/* types 3 and 4 are headphones */
		hsType = CS42L42_PLUG_HEADPHONE;
		sw = CS42L42_HSDET_SW_TYPE3;
	}
	write(CS42L42_HS_SWITCH_CTL, sw);
	/* manual, disabled; comparator levels back to their defaults */
	update(CS42L42_HSDET_CTL2, CS42L42_HSDET_CTL2_ALL, 0);
	write(CS42L42_HSDET_CTL1, (CS42L42_HSDET_COMP2_LVL_DEFAULT << 4) | CS42L42_HSDET_COMP1_LVL_DEFAULT);
	LOG("manual type detection: comparators %u/%u -> type %u", comp1, comp2, hsType);
}

/* cs42l42_process_hs_type_detect() */
void
SFCs42l83::processTypeDetect(void)
{
	uint8_t st = 0, discard;

	read(CS42L42_HS_DET_STATUS, &st);
	update(CS42L42_CODEC_INT_MASK, CS42L42_PDN_DONE_MASK | CS42L42_HSDET_AUTO_DONE_MASK,
	    CS42L42_PDN_DONE_MASK | CS42L42_HSDET_AUTO_DONE_MASK);
	hsType = st & CS42L42_HSDET_TYPE_MASK;
	LOG("automatic type detection: status 0x%02x -> type %u", st, hsType);
	/* automatic, disabled */
	update(CS42L42_HSDET_CTL2, CS42L42_HSDET_CTL2_ALL, (2 << 6) | (2 << 4) | 3);
	/* headphones and failures are looked at again by hand, so no headset microphone is missed */
	if (hsType == CS42L42_PLUG_INVALID || hsType == CS42L42_PLUG_HEADPHONE)
		manualTypeDetect();

	if (hsType == CS42L42_PLUG_CTIA || hsType == CS42L42_PLUG_OMTP) {
		/* button detection */
		update(CS42L42_HSBIAS_SC_AUTOCTL, CS42L42_HSBIAS_SENSE_EN_MASK | CS42L42_AUTO_HSBIAS_HIZ_MASK |
		    CS42L42_TIP_SENSE_EN_MASK | CS42L42_HSBIAS_SENSE_TRIP_MASK, 3);
		update(CS42L42_MIC_DET_CTL1, CS42L42_LATCH_TO_VP_MASK | CS42L42_EVENT_STAT_SEL_MASK | CS42L42_HS_DET_LEVEL_MASK,
		    CS42L42_LATCH_TO_VP_MASK | kBiasThresholds[0]);
		update(CS42L42_HSBIAS_SC_AUTOCTL, CS42L42_HSBIAS_SENSE_EN_MASK | CS42L42_AUTO_HSBIAS_HIZ_MASK |
		    CS42L42_TIP_SENSE_EN_MASK | CS42L42_HSBIAS_SENSE_TRIP_MASK,
		    (HS_BIAS_SENSE_EN << 7) | CS42L42_AUTO_HSBIAS_HIZ_MASK | 3);
		/* level detection on, bias at 2.7 V */
		update(CS42L42_MISC_DET_CTL, CS42L42_HSBIAS_CTL_MASK | CS42L42_PDN_MIC_LVL_DET_MASK, 3 << 1);
		IOSleep(BTN_DET_INIT_DBNCE_MS);
		read(CS42L42_DET_INT_STATUS2, &discard);
		/* button interrupts on: level changes and bias high-impedance; shorts stay masked */
		update(CS42L42_DET_INT2_MASK, CS42L42_DET_INT_VAL2_MASK, CS42L42_M_SHORT_RLS_MASK | CS42L42_M_SHORT_DET_MASK);
	} else {
		/* bias grounded, level detection off */
		update(CS42L42_MISC_DET_CTL, CS42L42_HSBIAS_CTL_MASK | CS42L42_PDN_MIC_LVL_DET_MASK,
		    (1 << 1) | CS42L42_PDN_MIC_LVL_DET_MASK);
	}
	/* output: no pull-down, no clamp, high-pass filter on */
	update(CS42L42_DAC_CTL2, CS42L42_DAC_CTL2_ALL, 1 << 1);
	update(CS42L42_TSRS_PLUG_INT_MASK, CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK, 0);
	detectsDone++;
}

/* cs42l42_init_hs_type_detect() */
void
SFCs42l83::initTypeDetect(void)
{
	if (remoteActive)
		remoteRestore(false);
	update(CS42L42_TSRS_PLUG_INT_MASK, CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK,
	    CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK);
	update(CS42L42_MISC_DET_CTL, CS42L42_HSBIAS_CTL_MASK | CS42L42_PDN_MIC_LVL_DET_MASK,
	    (1 << 1) | CS42L42_PDN_MIC_LVL_DET_MASK);
	update(CS42L42_HSBIAS_SC_AUTOCTL, CS42L42_HSBIAS_SENSE_EN_MASK | CS42L42_AUTO_HSBIAS_HIZ_MASK |
	    CS42L42_TIP_SENSE_EN_MASK | CS42L42_HSBIAS_SENSE_TRIP_MASK, 3);
	/* manual, disabled */
	update(CS42L42_HSDET_CTL2, CS42L42_HSDET_CTL2_ALL, (2 << 4) | 3);
	/* output pulled down and clamped while the plug is examined */
	update(CS42L42_DAC_CTL2, CS42L42_DAC_CTL2_ALL, (8 << 4) | (1 << 2) | (1 << 1) | 1);
	/* bias to 2.7 V, and let it ramp */
	update(CS42L42_MISC_DET_CTL, CS42L42_HSBIAS_CTL_MASK | CS42L42_PDN_MIC_LVL_DET_MASK,
	    (3 << 1) | CS42L42_PDN_MIC_LVL_DET_MASK);
	IOSleep(HS_BIAS_RAMP_TIME_MS);
	update(CS42L42_CODEC_INT_MASK, CS42L42_PDN_DONE_MASK | CS42L42_HSDET_AUTO_DONE_MASK, CS42L42_PDN_DONE_MASK);
	/* automatic, enabled */
	update(CS42L42_HSDET_CTL2, CS42L42_HSDET_CTL2_ALL, (3 << 6) | (2 << 4) | 3);
}

/* cs42l42_cancel_hs_type_detect() */
void
SFCs42l83::cancelTypeDetect(void)
{
	if (remoteActive)
		remoteRestore(false);
	update(CS42L42_DET_INT2_MASK, CS42L42_DET_INT_VAL2_MASK, CS42L42_DET_INT_VAL2_MASK);
	update(CS42L42_MISC_DET_CTL, CS42L42_HSBIAS_CTL_MASK | CS42L42_PDN_MIC_LVL_DET_MASK,
	    (1 << 1) | CS42L42_PDN_MIC_LVL_DET_MASK);
	update(CS42L42_HSBIAS_SC_AUTOCTL, CS42L42_HSBIAS_SENSE_EN_MASK | CS42L42_AUTO_HSBIAS_HIZ_MASK |
	    CS42L42_TIP_SENSE_EN_MASK | CS42L42_HSBIAS_SENSE_TRIP_MASK, 3);
	update(CS42L42_HSDET_CTL2, CS42L42_HSDET_CTL2_ALL, (2 << 4) | 3);
}

/* cs42l42_handle_button_press(): which of the four bias levels the button pulls below */
uint8_t
SFCs42l83::buttonPress(void)
{
	static const uint8_t kButton[5] = { 0, kSFJackButtonC, kSFJackButtonB, kSFJackButtonD, kSFJackButtonA };
	uint8_t st = 0;
	int level = 1;

	update(CS42L42_DET_INT2_MASK, CS42L42_DET_INT_VAL2_MASK, CS42L42_DET_INT_VAL2_MASK);
	IOSleep(BTN_DET_EVENT_DBNCE_MS);
	do {
		update(CS42L42_MIC_DET_CTL1, CS42L42_LATCH_TO_VP_MASK | CS42L42_EVENT_STAT_SEL_MASK | CS42L42_HS_DET_LEVEL_MASK,
		    CS42L42_LATCH_TO_VP_MASK | kBiasThresholds[level]);
		read(CS42L42_DET_STATUS2, &st);
	} while ((st & CS42L42_HS_TRUE_MASK) && ++level < 4);
	update(CS42L42_MIC_DET_CTL1, CS42L42_LATCH_TO_VP_MASK | CS42L42_EVENT_STAT_SEL_MASK | CS42L42_HS_DET_LEVEL_MASK,
	    CS42L42_LATCH_TO_VP_MASK | kBiasThresholds[0]);
	read(CS42L42_DET_INT_STATUS2, &st);
	update(CS42L42_DET_INT2_MASK, CS42L42_DET_INT_VAL2_MASK, CS42L42_M_SHORT_RLS_MASK | CS42L42_M_SHORT_DET_MASK);
	return kButton[level];
}

/* cs42l83_remote_restore(): back to the analogue button detector */
void
SFCs42l83::remoteRestore(bool settle)
{
	uint8_t discard;
	int i;

	/* nothing reports the release of a held remote button once the receiver is off */
	report(jack, 0, kSFJackButtonA | kSFJackButtonB | kSFJackButtonC);
	for (i = 0; i < kSavedInt1Mask; i++)
		write(kRemoteRegs[i], remoteSaved[i]);
	if (settle)
		IOSleep(BTN_DET_INIT_DBNCE_MS);
	read(CS42L42_DET_INT_STATUS1, &discard);
	read(CS42L42_DET_INT_STATUS2, &discard);
	for (; i < kSavedCount; i++)
		write(kRemoteRegs[i], remoteSaved[i]);
	remoteActive = false;
}

/*
 * cs42l83_remote_set_detect(): the bias reference is held across a change of bias
 * mode, and the transmit bit goes in only after the bias bits have settled.
 */
int
SFCs42l83::remoteSetDetect(uint8_t value)
{
	uint8_t old = 0;
	int err = read(CS42L42_MISC_DET_CTL, &old);
	bool biasChanged = (old & CS42L42_HSBIAS_CTL_MASK) != (value & CS42L42_HSBIAS_CTL_MASK);

	if (err == 0 && biasChanged) {
		err = update(CS42L42_HS_BIAS_CTL, CS42L83_HS_BIAS_CTL_TRANSITION, 0);
		if (err == 0)
			err = update(CS42L42_HSDET_CTL2, CS42L42_HSBIAS_REF_MASK, CS42L42_HSBIAS_REF_MASK);
	}
	if (err == 0)
		err = update(CS42L42_MISC_DET_CTL, (uint8_t)(0xff & ~(value & CS42L83_REMOTE_TX_START)), value);
	if (err == 0 && ((old ^ value) & CS42L83_HSBIAS_CTL_ON))
		IOSleep(10);
	if (err == 0 && (value & CS42L83_REMOTE_TX_START))
		err = write(CS42L42_MISC_DET_CTL, value);
	if (err == 0 && biasChanged)
		err = update(CS42L42_HSDET_CTL2, CS42L42_HSBIAS_REF_MASK, 0);
	return err;
}

/* cs42l83_remote_start(): identify an Apple inline remote; any failure keeps the analogue detector */
void
SFCs42l83::remoteStart(void)
{
	uint8_t v = 0, st1 = 0, st2 = 0;
	unsigned id;
	int err = 0, i;

	if (hsType != CS42L42_PLUG_CTIA && hsType != CS42L42_PLUG_OMTP)
		return;
	for (i = 0; i < kSavedCount; i++)
		if (read(kRemoteRegs[i], &remoteSaved[i]) != 0)
			return;
	err = write(CS42L42_DET_INT2_MASK, 0xff);
	if (err == 0)
		err = write(CS42L42_HSBIAS_SC_AUTOCTL, CS42L83_REMOTE_SENSE_TRIP);
	if (err == 0)
		err = write(CS42L42_WAKE_CTL, CS42L42_M_HP_WAKE_MASK | CS42L42_M_MIC_WAKE_MASK);
	if (err == 0)
		err = write(CS42L42_MIC_DET_CTL2, CS42L83_REMOTE_RX_CONFIG);
	/* bias reset, then the identification threshold */
	if (err == 0)
		err = remoteSetDetect(CS42L83_REMOTE_BIAS_RESET);
	IOSleep(10);
	if (err == 0)
		err = update(CS42L42_MIC_DET_CTL1, CS42L42_HS_DET_LEVEL_MASK, CS42L83_REMOTE_LEVEL_IDENTIFY);
	if (err == 0)
		err = read(CS42L42_DET_INT_STATUS1, &v);
	if (err == 0)
		err = read(CS42L42_DET_INT_STATUS2, &v);
	if (err == 0)
		err = remoteSetDetect(CS42L83_REMOTE_BIAS_IDENTIFY);
	/* wait for the receive acknowledgement, at most half a second */
	if (err == 0) {
		IOSleep(CS42L83_REMOTE_SETTLE_MS);
		err = ETIMEDOUT;
		for (i = 0; i < 100; i++) {
			if (read(CS42L42_DET_INT_STATUS2, &v) != 0) {
				err = EIO;
				break;
			}
			if (v & CS42L83_REMOTE_RX_ACK) {
				err = 0;
				break;
			}
			IOSleep(5);
		}
	}
	if (err == 0)
		err = read(CS42L42_DET_INT_STATUS1, &v);
	if (err == 0)
		err = read(CS42L42_DET_STATUS1, &st1);
	if (err == 0)
		err = read(CS42L42_DET_STATUS2, &st2);
	/* a short or an invalid identifier keeps the analogue detector */
	if (err == 0 && (st2 & CS42L42_SHORT_TRUE_MASK))
		err = ENODEV;
	id = (unsigned)(st2 >> 4) | ((unsigned)(st1 & 0x1f) << 4);
	for (i = 0; err == 0 && i < 3; i++) {
		unsigned symbol = (id >> (i * 3)) & 7;
		if (symbol < 1 || symbol > 4)
			err = ENODEV;
	}
	if (err == 0)
		err = update(CS42L42_MIC_DET_CTL1, CS42L42_HS_DET_LEVEL_MASK, CS42L83_REMOTE_LEVEL_BUTTONS);
	if (err == 0)
		err = remoteSetDetect(CS42L83_REMOTE_BIAS_BUTTONS);
	if (err == 0) {
		IOSleep(CS42L83_REMOTE_SETTLE_MS);
		err = write(CS42L42_HSBIAS_SC_AUTOCTL, (HS_BIAS_SENSE_EN << 7) | CS42L83_REMOTE_SENSE_TRIP);
	}
	if (err == 0)
		err = read(CS42L42_DET_INT_STATUS1, &v);
	if (err == 0)
		err = read(CS42L42_DET_INT_STATUS2, &v);
	if (err == 0)
		err = write(CS42L42_DET_INT1_MASK, (uint8_t)(remoteSaved[kSavedInt1Mask] & ~(HS_BIAS_SENSE_EN << 7)));
	if (err == 0)
		err = write(CS42L42_DET_INT2_MASK,
		    (uint8_t)(0xff & ~(CS42L83_REMOTE_BUTTON_IRQ | CS42L42_M_SHORT_DET_MASK | CS42L42_M_SHORT_RLS_MASK)));
	if (err == 0) {
		remoteActive = true;
		LOG("Apple headset remote enabled (identifier 0x%03x)", id);
		return;
	}
	LOG("no Apple headset remote (error %d, identifier 0x%03x); analogue button detection", err, id);
	remoteRestore(true);
}

/* cs42l83_remote_buttons() */
void
SFCs42l83::remoteButtons(uint8_t int1, uint8_t int2)
{
	if (int1 & CS42L83_REMOTE_BUTTONS) {
		uint8_t b = 0;
		/* volume indications are pulses without a release */
		if (int1 & CS42L83_REMOTE_VOLUME_DOWN)
			b |= kSFJackButtonC;
		if (int1 & CS42L83_REMOTE_VOLUME_UP)
			b |= kSFJackButtonB;
		if (b)
			report(jack, b, kSFJackButtonB | kSFJackButtonC);
		report(jack, 0, kSFJackButtonB | kSFJackButtonC);
	}
	if (int2 & CS42L42_M_SHORT_DET_MASK)
		report(jack, kSFJackButtonA, kSFJackButtonA);
	if (int2 & CS42L42_M_SHORT_RLS_MASK)
		report(jack, 0, kSFJackButtonA);
}

/* status register, mask register, the bits that are interrupts (irq_params_table) */
static const struct {
	uint16_t status, mask;
	uint8_t  bits;
} kIrq[12] = {
	{ CS42L42_ADC_OVFL_STATUS, CS42L42_ADC_OVFL_INT_MASK, 0x01 },
	{ CS42L42_MIXER_STATUS, CS42L42_MIXER_INT_MASK, 0x0f },
	{ CS42L42_SRC_STATUS, CS42L42_SRC_INT_MASK, 0x0f },
	{ CS42L42_ASP_RX_STATUS, CS42L42_ASP_RX_INT_MASK, 0x1f },
	{ CS42L42_ASP_TX_STATUS, CS42L42_ASP_TX_INT_MASK, 0x0f },
	{ CS42L42_CODEC_STATUS, CS42L42_CODEC_INT_MASK, 0x03 },
	{ CS42L42_DET_INT_STATUS1, CS42L42_DET_INT1_MASK, 0xe0 },
	{ CS42L42_DET_INT_STATUS2, CS42L42_DET_INT2_MASK, CS42L42_DET_INT_VAL2_MASK },
	{ CS42L42_SRCPL_INT_STATUS, CS42L42_SRCPL_INT_MASK, 0x65 },
	{ CS42L42_VPMON_STATUS, CS42L42_VPMON_INT_MASK, 0x01 },
	{ CS42L42_PLL_LOCK_STATUS, CS42L42_PLL_LOCK_INT_MASK, 0x01 },
	{ CS42L42_TSRS_PLUG_STATUS, CS42L42_TSRS_PLUG_INT_MASK, 0x0f },
};

/* cs42l42_irq_thread() */
void
SFCs42l83::jackService(void)
{
	uint8_t stickies[12], masks[12], rawRemote = 0, plug, button;
	const uint8_t allButtons = kSFJackButtonA | kSFJackButtonB | kSFJackButtonC | kSFJackButtonD;

	if (!ready)
		return;
	IOLockLock(jackLock);
	for (unsigned i = 0; i < 12; i++) {
		if (read(kIrq[i].status, &stickies[i]) != 0 || read(kIrq[i].mask, &masks[i]) != 0) {
			IOLockUnlock(jackLock);
			return;
		}
		if (remoteActive && i == 6)
			rawRemote = stickies[i] & CS42L83_REMOTE_BUTTONS;
		stickies[i] &= (uint8_t)~masks[i] & kIrq[i].bits;
	}
	irqServices++;
	plug = (stickies[11] & (CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK)) >> CS42L42_TS_PLUG_SHIFT;
	button = stickies[7] & (CS42L42_M_DETECT_TF_MASK | CS42L42_M_DETECT_FT_MASK | CS42L42_M_HSBIAS_HIZ_MASK);

	/* a finished type detection, whatever came before it */
	if ((~masks[5] & kIrq[5].bits) && (stickies[5] & CS42L42_HSDET_AUTO_DONE_MASK)) {
		processTypeDetect();
		/* the identification toggles the bias: what was latched so far is no key press */
		remoteStart();
		button = 0;
		stickies[7] = 0;
		rawRemote = 0;
		if (plugState == CS42L42_TS_PLUG) {
			if (hsType == CS42L42_PLUG_CTIA || hsType == CS42L42_PLUG_OMTP)
				report(kSFJackHeadset, 0, allButtons);
			else if (hsType == CS42L42_PLUG_HEADPHONE)
				report(kSFJackHeadphone, 0, allButtons);
		}
	}
	/* a power-down-done interrupt is only ever unmasked by jackStart(selfTest) */
	if ((~masks[5] & CS42L42_PDN_DONE_MASK))
		update(CS42L42_CODEC_INT_MASK, CS42L42_PDN_DONE_MASK, CS42L42_PDN_DONE_MASK);

	if (~masks[11] & kIrq[11].bits) {
		if (plug == CS42L42_TS_PLUG) {
			if (plugState != CS42L42_TS_PLUG) {
				plugState = CS42L42_TS_PLUG;
				plugEvents++;
				initTypeDetect();
			}
		} else if (plug == CS42L42_TS_UNPLUG) {
			if (plugState != CS42L42_TS_UNPLUG) {
				plugState = CS42L42_TS_UNPLUG;
				unplugEvents++;
				cancelTypeDetect();
				hsType = CS42L42_PLUG_INVALID;
				report(kSFJackNone, 0, allButtons);
			}
		} else {
			plugState = CS42L42_TS_TRANS;
		}
	}

	if (remoteActive && plugState == CS42L42_TS_PLUG) {
		remoteButtons(rawRemote, stickies[7]);
	} else if (plugState == CS42L42_TS_PLUG && (~masks[7] & kIrq[7].bits) && !(button & CS42L42_M_HSBIAS_HIZ_MASK)) {
		if (button & CS42L42_M_DETECT_TF_MASK)
			report(jack, 0, allButtons);
		else if (button & CS42L42_M_DETECT_FT_MASK)
			report(jack, buttonPress(), allButtons);
	}
	IOLockUnlock(jackLock);
}

int
SFCs42l83::jackStart(bool selfTest)
{
	uint8_t st = 0;
	int err;

	if (!ready)
		return ENXIO;
	IOLockLock(jackLock);
	hsType = CS42L42_PLUG_INVALID;
	/* cs42l42_setup_hs_type_detect(): the tip sense as it is now */
	err = read(CS42L42_TSRS_PLUG_STATUS, &st);
	plugState = (st & (CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK)) >> CS42L42_TS_PLUG_SHIFT;
	/*
	 * cs42l42_set_interrupt_masks(): everything masked but the tip sense; the ring
	 * sense, which this board does not wire, stays masked.
	 */
	if (err == 0)
		err = write(CS42L42_ADC_OVFL_INT_MASK, 0x01);
	if (err == 0)
		err = write(CS42L42_MIXER_INT_MASK, 0x0f);
	if (err == 0)
		err = write(CS42L42_SRC_INT_MASK, 0x0f);
	if (err == 0)
		err = write(CS42L42_ASP_RX_INT_MASK, 0x1f);
	if (err == 0)
		err = write(CS42L42_ASP_TX_INT_MASK, 0x0f);
	if (err == 0)
		err = update(CS42L42_CODEC_INT_MASK, CS42L42_PDN_DONE_MASK | CS42L42_HSDET_AUTO_DONE_MASK,
		    (selfTest ? 0 : CS42L42_PDN_DONE_MASK) | CS42L42_HSDET_AUTO_DONE_MASK);
	if (err == 0)
		err = update(CS42L42_SRCPL_INT_MASK, 0x65, 0x65);
	if (err == 0)
		err = update(CS42L42_DET_INT1_MASK, 0xe0, 0xe0);
	if (err == 0)
		err = update(CS42L42_DET_INT2_MASK, CS42L42_DET_INT_VAL2_MASK, CS42L42_DET_INT_VAL2_MASK);
	if (err == 0)
		err = write(CS42L42_VPMON_INT_MASK, 0x01);
	if (err == 0)
		err = write(CS42L42_PLL_LOCK_INT_MASK, 0x01);
	if (err == 0)
		err = update(CS42L42_TSRS_PLUG_INT_MASK, 0x0f, CS42L42_RS_PLUG_MASK | CS42L42_RS_UNPLUG_MASK);
	/* a plug that was in before the driver came: no plug event will follow, detect now */
	if (err == 0 && plugState == CS42L42_TS_PLUG) {
		plugEvents++;
		initTypeDetect();
	}
	IOLockUnlock(jackLock);
	return err;
}

bool
SFCs42l83::jackSelfTest(void)
{
	bool run;

	if (!ready)
		return false;
	IOLockLock(jackLock);
	run = plugState != CS42L42_TS_PLUG;
	if (run)
		initTypeDetect();
	IOLockUnlock(jackLock);
	if (!run)
		return false;
	/* the detection reports through the interrupt; jackService() handles it like any other */
	IOSleep(1500);
	IOLockLock(jackLock);
	if (plugState != CS42L42_TS_PLUG) {
		cancelTypeDetect();
		/* output as the reset left it; the tip sense interrupts on again */
		update(CS42L42_DAC_CTL2, CS42L42_DAC_CTL2_ALL, 0x02);
		update(CS42L42_TSRS_PLUG_INT_MASK, CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK, 0);
		hsType = CS42L42_PLUG_INVALID;
	}
	IOLockUnlock(jackLock);
	return true;
}
