// SPDX-License-Identifier: GPL-2.0-only
/*
 * Touch Bar display. Ported from Linux as carried in omacom/linux-aurora:
 *   drivers/gpu/drm/adp/adp_drv.c (adp_drm_bind, adp_crtc_atomic_enable,
 *     adp_plane_atomic_update, adp_crtc_atomic_flush, adp_enable_vblank), GPL-2.0-only
 *   drivers/gpu/drm/adp/adp-mipi.c (adp_dsi_write, adp_dsi_gen_pkt_hdr_write, adp_dsi_read),
 *     GPL-2.0-only
 *   drivers/gpu/drm/panel/panel-summit.c (summit_set_brightness, summit_mode), GPL-2.0-only
 *   Copyright The Asahi Linux Contributors (Sasha Finkelstein, Janne Grunau, Nick Chan)
 * The fetch probe and the survey are new.
 */
#include "sf_adp.h"

#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <string.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFAdp: " fmt "\n", ##__VA_ARGS__)

/* register blocks of /arm-io's Touch Bar display (Linux t8103.dtsi display_dfr, displaydfr_mipi) */
#define ADP_BE_PHYS     0x228200000ULL
#define ADP_BE_LEN      0xc000
#define ADP_FE_PHYS     0x228400000ULL
#define ADP_FE_LEN      0x4000
#define ADP_MIPI_PHYS   0x228600000ULL
#define ADP_MIPI_LEN    0x4000
#define ADP_DART        "/arm-io/dart-dispdfr"
#define ADP_DART_SID    0
/* the window of this driver in the stream's address space: frame buffer, then mask */
#define ADP_IOVA        0x00800000u
#define ADP_IOVA_HOLE   0x10000000u

#define ADP_INT_STATUS          0x34
#define ADP_INT_STATUS_INT_MASK 0x7
#define ADP_INT_STATUS_VBLANK   0x1
#define ADP_CTRL                0x100
#define ADP_CTRL_VBLANK_ON      0x12
#define ADP_CTRL_FIFO_ON        0x601
#define ADP_SCREEN_SIZE         0x0c

#define ADBE_FIFO               0x10c0
#define ADBE_FIFO_SYNC          0xc0000000u
#define ADBE_BLEND_BYPASS       0x2020
#define ADBE_BLEND_EN1          0x2028
#define ADBE_BLEND_EN2          0x2074
#define ADBE_BLEND_EN3          0x202c
#define ADBE_BLEND_EN4          0x2034
#define ADBE_MASK_BUF           0x2200
#define ADBE_SRC_START          0x4040
#define ADBE_SRC_SIZE           0x4048
#define ADBE_DST_START          0x4050
#define ADBE_DST_SIZE           0x4054
#define ADBE_STRIDE             0x4038
#define ADBE_FB_BASE            0x4030
#define ADBE_LAYER_EN1          0x4020
#define ADBE_LAYER_EN2          0x4068
#define ADBE_LAYER_EN3          0x40b4
#define ADBE_LAYER_EN4          0x40f4
#define ADBE_SCALE_CTL          0x40ac
#define ADBE_SCALE_CTL_BYPASS   0x100000
#define ADBE_LAYER_CTL          0x1038
#define ADBE_LAYER_CTL_ENABLE   0x10000
#define ADBE_PIX_FMT            0x402c
#define ADBE_PIX_FMT_XRGB32     0x53e4001

#define DSI_GEN_HDR             0x6c
#define DSI_GEN_PLD_DATA        0x70
#define DSI_CMD_PKT_STATUS      0x74
#define GEN_PLD_R_EMPTY         (1u << 4)
#define GEN_PLD_W_FULL          (1u << 3)
#define GEN_PLD_W_EMPTY         (1u << 2)
#define GEN_CMD_FULL            (1u << 1)
#define GEN_CMD_EMPTY           (1u << 0)
#define GEN_RD_CMD_BUSY         (1u << 6)
#define CMD_PKT_STATUS_TIMEOUT_US 20000

#define MIPI_DSI_DCS_READ               0x06
#define MIPI_DSI_DCS_LONG_WRITE         0x39
#define MIPI_DCS_SET_DISPLAY_BRIGHTNESS 0x51

#define PAGE_16K 0x4000u
/* how long the pipe runs after a frame was presented (DRM turns vertical blanks off after 5 s) */
#define ADP_RUN_MS 1000

static uint64_t
now_ms(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000000;
}

bool
SFAdp::survey()
{
	/* every domain's raw state first: a block is only read while its domain is on */
	uint32_t feps = sf_pmgr_state_named("DISPDFR_FE"), beps = sf_pmgr_state_named("DISPDFR_BE"),
	    dsips = sf_pmgr_state_named("MIPI_DSI");
	LOG("as the boot loader left it: pmgr DISPDFR_FE=0x%08x DISPDFR_BE=0x%08x MIPI_DSI=0x%08x", feps, beps, dsips);
	if ((feps & 0xf0) == 0xf0 && fe.mapPhys(ADP_FE_PHYS, ADP_FE_LEN))
		LOG("  front end: size=0x%08x (%u x %u) ctrl=0x%08x int=0x%08x", fe.r32(ADP_SCREEN_SIZE),
		    fe.r32(ADP_SCREEN_SIZE) & 0xffff, fe.r32(ADP_SCREEN_SIZE) >> 16, fe.r32(ADP_CTRL), fe.r32(ADP_INT_STATUS));
	if ((beps & 0xf0) == 0xf0 && be.mapPhys(ADP_BE_PHYS, ADP_BE_LEN))
		LOG("  back end: layer_ctl=0x%08x layer_en=%x/%x/%x/%x fb=0x%08x stride=0x%x pixfmt=0x%08x src=0x%08x "
		    "dst=0x%08x blend=%x/%x/%x/%x bypass=%x mask=0x%08x", be.r32(ADBE_LAYER_CTL), be.r32(ADBE_LAYER_EN1),
		    be.r32(ADBE_LAYER_EN2), be.r32(ADBE_LAYER_EN3), be.r32(ADBE_LAYER_EN4), be.r32(ADBE_FB_BASE),
		    be.r32(ADBE_STRIDE), be.r32(ADBE_PIX_FMT), be.r32(ADBE_SRC_SIZE), be.r32(ADBE_DST_SIZE),
		    be.r32(ADBE_BLEND_EN1), be.r32(ADBE_BLEND_EN2), be.r32(ADBE_BLEND_EN3), be.r32(ADBE_BLEND_EN4),
		    be.r32(ADBE_BLEND_BYPASS), be.r32(ADBE_MASK_BUF));
	if ((dsips & 0xf0) == 0xf0 && mipi.mapPhys(ADP_MIPI_PHYS, ADP_MIPI_LEN))
		LOG("  dsi host: version=0x%08x pwr_up=%x mode_cfg=%x vid_mode=0x%x pkt_size=%u vactive=%u cmd_mode=0x%08x "
		    "last_hdr=0x%x cmd_status=0x%x phy_rstz=0x%x phy_status=0x%x lpclk=%x", mipi.r32(0x00), mipi.r32(0x04),
		    mipi.r32(0x34), mipi.r32(0x38), mipi.r32(0x3c), mipi.r32(0x60), mipi.r32(0x68), mipi.r32(DSI_GEN_HDR),
		    mipi.r32(DSI_CMD_PKT_STATUS), mipi.r32(0xa0), mipi.r32(0xb0), mipi.r32(0x94));
	if ((feps & 0xf0) == 0xf0) {
		SFMmio d;
		if (d.mapNode(ADP_DART, 0)) {
			LOG("  dart: params1=0x%08x config=0x%08x enable=0x%08x tcr0=0x%08x ttbr0=0x%08x error=0x%08x", d.r32(0x00),
			    d.r32(0x60), d.r32(0xfc), d.r32(0x100), d.r32(0x200), d.r32(0x40));
			char tcrs[200], ttbrs[200];
			size_t a = 0, b = 0;
			for (int sid = 0; sid < 16; sid++) {
				a += snprintf(tcrs + a, sizeof(tcrs) - a, " %x", d.r32(0x100 + 4 * sid));
				b += snprintf(ttbrs + b, sizeof(ttbrs) - b, " %x", d.r32(0x200 + 16 * sid));
			}
			LOG("  dart streams 0..15: tcr%s; ttbr0%s", tcrs, ttbrs);
			d.unmap();
		}
	}
	return fe.va != NULL && be.va != NULL && mipi.va != NULL;
}

int
SFAdp::attach(uint32_t expectWidth, uint32_t expectHeight, uint32_t line)
{
	int err;

	/* the Linux node's power-domains: dispdfr_fe, dispdfr_be; the DSI host's: mipi_dsi */
	if (sf_pmgr_enable_named("DISPDFR_FE", true) != 0 || sf_pmgr_enable_named("DISPDFR_BE", true) != 0 ||
	    sf_pmgr_enable_named("MIPI_DSI", true) != 0)
		return ENXIO;
	if ((fe.va == NULL && !fe.mapPhys(ADP_FE_PHYS, ADP_FE_LEN)) || (be.va == NULL && !be.mapPhys(ADP_BE_PHYS, ADP_BE_LEN)) ||
	    (mipi.va == NULL && !mipi.mapPhys(ADP_MIPI_PHYS, ADP_MIPI_LEN)))
		return ENXIO;

	/* adp_setup_mode_config(): the pipe knows the panel's size */
	uint32_t size = fe.r32(ADP_SCREEN_SIZE);
	width = size & 0xffff;
	height = size >> 16;
	if (width != expectWidth || height != expectHeight) {
		LOG("the pipe reports %u x %u, not the %u x %u of the Summit panel; display left alone", width, height,
		    expectWidth, expectHeight);
		return ENOTSUP;
	}
	if (line < width || (line & 15) != 0)
		return EINVAL;
	linePixels = line;
	fbBytes = (linePixels * height * 4 + PAGE_16K - 1) & ~(PAGE_16K - 1);
	/* adp_crtc_atomic_flush(): the mask is hdisplay * vdisplay * 4 bytes, page aligned */
	maskBytes = (width * height * 4 + PAGE_16K - 1) & ~(PAGE_16K - 1);

	err = dart.attach(ADP_DART, ADP_DART_SID, ADP_IOVA, fbBytes + maskBytes);
	if (err != 0)
		return err;
	/* the pipe may have fetched through the stream's old tables while they were replaced */
	dart.check("stream taken over");
	dart.faults = 0;
	fb = (uint32_t *)dart.buf;
	fbIova = ADP_IOVA;
	maskIova = ADP_IOVA + fbBytes;
	memset(dart.buf + fbBytes, 0xff, maskBytes);            /* adp_crtc_atomic_flush(): mask all ones */

	/* adp_drm_bind() */
	fe.w32(ADP_CTRL, ADP_CTRL_FIFO_ON);

	/* adp_plane_atomic_update(): the whole frame buffer, unscaled */
	be.w32(ADBE_SRC_START, 0);
	be.w32(ADBE_SRC_SIZE, width << 16 | height);
	be.w32(ADBE_DST_START, 0);
	be.w32(ADBE_DST_SIZE, width << 16 | height);
	be.w32(ADBE_STRIDE, linePixels * 4);
	be.w32(ADBE_FB_BASE, fbIova);
	be.w32(ADBE_LAYER_EN1, 1);
	be.w32(ADBE_LAYER_EN2, 1);
	be.w32(ADBE_LAYER_EN3, 1);
	be.w32(ADBE_LAYER_EN4, 1);
	be.w32(ADBE_SCALE_CTL, ADBE_SCALE_CTL_BYPASS);
	be.w32(ADBE_LAYER_CTL, ADBE_LAYER_CTL_ENABLE | 1);
	be.w32(ADBE_PIX_FMT, ADBE_PIX_FMT_XRGB32);
	/* adp_crtc_atomic_flush() */
	be.w32(ADBE_MASK_BUF, maskIova);
	be.w32(ADBE_FIFO, ADBE_FIFO_SYNC | 1);
	/* adp_crtc_atomic_enable(), which a first commit runs after the planes */
	be.w32(ADBE_BLEND_EN2, 1);
	be.w32(ADBE_BLEND_EN1, 1u << 4);
	be.w32(ADBE_BLEND_EN3, 1);
	be.w32(ADBE_BLEND_BYPASS, 1);
	be.w32(ADBE_BLEND_EN4, 1);
	up = true;
	flush();
	return 0;
}

void
SFAdp::flush()
{
	if (!up)
		return;
	__asm__ volatile("dsb sy" ::: "memory");
	/*
	 * adp_enable_vblank(), which every DRM commit goes through on Linux (the flush takes a
	 * vertical blank reference): with the front end left at ADP_CTRL_FIFO_ON alone the
	 * pipe does not fetch the layer (boot tb01: no DART fault from an unmapped layer).
	 */
	fe.w32(ADP_INT_STATUS, ADP_INT_STATUS_INT_MASK);
	fe.w32(ADP_CTRL, fe.r32(ADP_CTRL) | ADP_CTRL_VBLANK_ON);
	be.w32(ADBE_FIFO, ADBE_FIFO_SYNC | 1);
	runUntilMs = now_ms() + ADP_RUN_MS;
	flushes++;
}

void
SFAdp::tick()
{
	if (!up || runUntilMs == 0 || now_ms() < runUntilMs)
		return;
	/* adp_disable_vblank() */
	fe.w32(ADP_CTRL, fe.r32(ADP_CTRL) & ~ADP_CTRL_VBLANK_ON);
	fe.w32(ADP_INT_STATUS, ADP_INT_STATUS_INT_MASK);
	runUntilMs = 0;
}

static bool
dsi_wait(const SFMmio &mipi, uint32_t mask, uint32_t want)
{
	/* readl_poll_timeout(..., 1000, CMD_PKT_STATUS_TIMEOUT_US) */
	for (int us = 0; us <= CMD_PKT_STATUS_TIMEOUT_US; us += 100) {
		if ((mipi.r32(DSI_CMD_PKT_STATUS) & mask) == want)
			return true;
		IODelay(100);
	}
	return false;
}

/* adp_dsi_write() + adp_dsi_gen_pkt_hdr_write(): payload words, then the packet header */
static int
dsi_send(const SFMmio &mipi, uint8_t type, const uint8_t *payload, unsigned len, uint16_t shortData)
{
	for (unsigned off = 0; off < len; off += 4) {
		uint32_t word = 0;
		memcpy(&word, payload + off, len - off < 4 ? len - off : 4);
		mipi.w32(DSI_GEN_PLD_DATA, word);
		if (!dsi_wait(mipi, GEN_PLD_W_FULL, 0))
			return ETIMEDOUT;
	}
	if (!dsi_wait(mipi, GEN_CMD_FULL, 0))
		return ETIMEDOUT;
	/* header bytes: data type (virtual channel 0), then the word count or the two data bytes */
	mipi.w32(DSI_GEN_HDR, type | (uint32_t)(len ? len : shortData) << 8);
	if (!dsi_wait(mipi, GEN_CMD_EMPTY | GEN_PLD_W_EMPTY, GEN_CMD_EMPTY | GEN_PLD_W_EMPTY))
		return ETIMEDOUT;
	return 0;
}

int
SFAdp::setBrightness(unsigned level)
{
	/* mipi_dsi_dcs_set_display_brightness(): command and the level, little-endian */
	uint8_t payload[3] = { MIPI_DCS_SET_DISPLAY_BRIGHTNESS, (uint8_t)(level > 255 ? 255 : level), 0 };

	if (mipi.va == NULL)
		return ENXIO;
	int err = dsi_send(mipi, MIPI_DSI_DCS_LONG_WRITE, payload, sizeof(payload), 0);
	if (err != 0) {
		dsiErrors++;
		LOG("brightness %u: the DSI command FIFO did not drain (status 0x%x)", level, mipi.r32(DSI_CMD_PKT_STATUS));
		return err;
	}
	brightness = payload[1];
	return 0;
}

int
SFAdp::dcsRead(uint8_t command, uint8_t *out, unsigned len)
{
	if (mipi.va == NULL || len == 0 || len > 4)
		return EINVAL;
	int err = dsi_send(mipi, MIPI_DSI_DCS_READ, NULL, 0, command);
	/* adp_dsi_read() */
	if (err == 0 && !dsi_wait(mipi, GEN_RD_CMD_BUSY, 0))
		err = ETIMEDOUT;
	if (err == 0 && !dsi_wait(mipi, GEN_PLD_R_EMPTY, 0))
		err = ENODATA;
	if (err != 0) {
		dsiErrors++;
		return err;
	}
	uint32_t val = mipi.r32(DSI_GEN_PLD_DATA);
	for (unsigned i = 0; i < len; i++)
		out[i] = (uint8_t)(val >> (8 * i));
	return 0;
}

uint32_t
SFAdp::vblankHz(unsigned ms)
{
	uint32_t count = 0;

	if (!up || ms == 0)
		return 0;
	/* adp_enable_vblank(); nobody takes the front end's interrupt, the flag is polled */
	fe.w32(ADP_INT_STATUS, ADP_INT_STATUS_INT_MASK);
	fe.w32(ADP_CTRL, fe.r32(ADP_CTRL) | ADP_CTRL_VBLANK_ON);
	uint64_t start = now_ms(), end = start;
	while (end - start < ms) {
		uint32_t st = fe.r32(ADP_INT_STATUS);
		if (st & ADP_INT_STATUS_VBLANK)
			count++;
		if (st & ADP_INT_STATUS_INT_MASK)
			fe.w32(ADP_INT_STATUS, st);
		IOSleep(1);
		end = now_ms();
	}
	runUntilMs = now_ms() + ADP_RUN_MS;         /* tick() turns the bits off again */
	return (uint32_t)((uint64_t)count * 1000 / (end - start));
}

uint32_t
SFAdp::fetchProbe(unsigned ms, bool running, uint64_t *addr)
{
	uint32_t seen = 0;
	/*
	 * An address with no second-level table. (An unmapped page inside this driver's own
	 * second-level table raises no fault on this DART, boot tb03: the layer at 0xc00000
	 * gave error 0, at 0x10000000 error 0x80000002, through the register and through the
	 * FIFO alike; the mask at either gave none, so the mask is not fetched.)
	 */
	const uint32_t hole = ADP_IOVA_HOLE;

	*addr = 0;
	if (!up)
		return 0;
	dart.check("before the fetch probe");
	uint32_t ctrl = fe.r32(ADP_CTRL);
	fe.w32(ADP_CTRL, running ? (ctrl | ADP_CTRL_VBLANK_ON) : (ctrl & ~ADP_CTRL_VBLANK_ON));
	be.w32(ADBE_FB_BASE, hole);
	be.w32(ADBE_FIFO, ADBE_FIFO_SYNC | 1);
	for (unsigned t = 0; t < ms && seen == 0; t += 5) {
		IOSleep(5);
		uint32_t err = dart.regs.r32(0x40);
		if (err & (1u << 31)) {
			*addr = ((uint64_t)dart.regs.r32(0x54) << 32) | dart.regs.r32(0x50);
			seen = err;
		}
	}
	be.w32(ADBE_FB_BASE, fbIova);
	flush();
	IOSleep(60);
	/* clear what the probe caused; it is not counted as a fault of normal operation */
	uint32_t before = dart.faults;
	dart.check("fetch probe (expected)");
	dart.faults = before;
	return seen;
}

void
SFAdp::flushTrace(char *text, size_t size)
{
	uint32_t seen = 0, flags[3] = { 0, 0, 0 }, backUs = 0, firstOther = 0;
	uint32_t before = fe.r32(ADP_CTRL);

	if (!up) {
		snprintf(text, size, "down");
		return;
	}
	fe.w32(ADP_INT_STATUS, ADP_INT_STATUS_INT_MASK);
	fe.w32(ADP_CTRL, before | ADP_CTRL_VBLANK_ON);
	be.w32(ADBE_FIFO, ADBE_FIFO_SYNC | 1);
	for (uint32_t us = 0; us < 100000; us += 50) {
		uint32_t ctrl = fe.r32(ADP_CTRL), st = fe.r32(ADP_INT_STATUS);
		uint32_t field = (ctrl >> 8) & 0xf;
		seen |= 1u << field;
		if (field != 6 && firstOther == 0)
			firstOther = us + 1;
		if (field == 6 && firstOther != 0 && backUs == 0)
			backUs = us;
		for (int b = 0; b < 3; b++)
			flags[b] += (st >> b) & 1;
		if (st & ADP_INT_STATUS_INT_MASK)
			fe.w32(ADP_INT_STATUS, st);
		IODelay(50);
	}
	runUntilMs = now_ms() + ADP_RUN_MS;
	flushes++;
	snprintf(text, size, "ctrl before=0x%x after=0x%x; values of ctrl[11:8] seen in 100 ms after the flush: mask 0x%x, "
	    "left 6 after %u us, back at 6 after %u us; interrupt flags in that time: bit0 %u bit1 %u bit2 %u; "
	    "fe 0x1c=0x%08x 0x54=0x%08x be 0x1014=0x%x 0x1018=0x%x 0x101c=0x%x", before, fe.r32(ADP_CTRL), seen,
	    firstOther ? firstOther - 1 : 0, backUs, flags[0], flags[1], flags[2], fe.r32(0x1c), fe.r32(0x54),
	    be.r32(0x1014), be.r32(0x1018), be.r32(0x101c));
}

void
SFAdp::describe(char *text, size_t size)
{
	if (!up) {
		snprintf(text, size, "down");
		return;
	}
	snprintf(text, size, "size=%ux%u line=%u fe_ctrl=0x%x layer_ctl=0x%x layer_en=%x/%x/%x/%x fb=0x%08x stride=%u pixfmt=0x%x "
	    "src=0x%08x dst=0x%08x mask=0x%08x blend=%x/%x/%x/%x bypass=%x flushes=%u brightness=%d dsi_errors=%u "
	    "dsi_status=0x%x dart_tcr=0x%x dart_error=0x%08x dart_faults=%u", width, height, linePixels, fe.r32(ADP_CTRL),
	    be.r32(ADBE_LAYER_CTL), be.r32(ADBE_LAYER_EN1), be.r32(ADBE_LAYER_EN2), be.r32(ADBE_LAYER_EN3),
	    be.r32(ADBE_LAYER_EN4), be.r32(ADBE_FB_BASE), be.r32(ADBE_STRIDE), be.r32(ADBE_PIX_FMT), be.r32(ADBE_SRC_SIZE),
	    be.r32(ADBE_DST_SIZE), be.r32(ADBE_MASK_BUF), be.r32(ADBE_BLEND_EN1), be.r32(ADBE_BLEND_EN2),
	    be.r32(ADBE_BLEND_EN3), be.r32(ADBE_BLEND_EN4), be.r32(ADBE_BLEND_BYPASS), flushes, brightness, dsiErrors,
	    mipi.r32(DSI_CMD_PKT_STATUS), dart.regs.r32(0x100), dart.regs.r32(0x40), dart.faults);
}
