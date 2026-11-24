/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The Touch Bar's display: Apple display pipe ("dispdfr": front end, back end), its MIPI
 * DSI host and the Summit panel behind it, one fixed frame buffer. Ported from Linux
 * drivers/gpu/drm/adp/adp_drv.c, adp-mipi.c and drivers/gpu/drm/panel/panel-summit.c
 * (omacom/linux-aurora). Like the Linux driver it relies on what iBoot set up (clocks,
 * DSI link, panel power-on); nothing here can bring the panel up from a powered-off
 * DISPDFR_BE domain.
 *
 * All calls from one thread at a time.
 */
#ifndef _SF_ADP_H
#define _SF_ADP_H

#include "sf_dart_fixed.h"
#include "sf_soc.h"

struct SFAdp {
	SFMmio      be, fe, mipi;
	SFDartFixed dart;
	uint32_t   *fb;             /* `height` lines of `linePixels` XRGB8888 pixels, `width` of them shown */
	uint32_t    width, height;  /* as the pipe reports them: 60 x 2008 */
	uint32_t    linePixels;
	uint32_t    fbIova, maskIova, fbBytes, maskBytes;
	uint64_t    runUntilMs;     /* the pipe runs (vertical blank bits on) until then */
	uint32_t    flushes;
	uint32_t    dsiErrors;
	int         brightness;     /* last level written, -1 before the first */
	bool        up;

	SFAdp() : fb(NULL), width(0), height(0), linePixels(0), fbIova(0), maskIova(0), fbBytes(0), maskBytes(0),
	    runUntilMs(0), flushes(0), dsiErrors(0),
	    brightness(-1), up(false) {}

	/* Logs the state the boot loader left (read only). False when the blocks cannot be mapped. */
	bool survey();
	/*
	 * Powers the domains, takes DART stream 0, programs the pipe for a frame buffer of
	 * lines `linePixels` long (a multiple of 16: the pipe takes its stride in units of 64
	 * bytes). 0 or errno.
	 */
	int  attach(uint32_t expectWidth, uint32_t expectHeight, uint32_t linePixels);
	/*
	 * Presents the frame buffer's current content (adp_crtc_atomic_flush) and keeps the pipe
	 * running for a while, as a DRM commit does through its vertical blank wait.
	 */
	void flush();
	/* Stops the pipe once the time after the last flush is over (the panel keeps its picture). */
	void tick();
	/* Panel brightness 0..255 (MIPI DCS set_display_brightness). 0 or errno. */
	int  setBrightness(unsigned level);
	/* DCS read of `len` bytes (at most 4); 0 or errno. */
	int  dcsRead(uint8_t command, uint8_t *out, unsigned len);
	/* The front end's vertical blank flags per second, measured over `ms` milliseconds. */
	uint32_t vblankHz(unsigned ms);
	/*
	 * Points the layer at an address the DART does not map for `ms`, then back: returns the
	 * DART's error register seen meanwhile (0: the pipe did not fetch from there) and the
	 * faulting address. `running`: with the vertical blank bits on, as after a flush;
	 * otherwise with the front end as adp_drm_bind() leaves it.
	 */
	uint32_t fetchProbe(unsigned ms, bool running, uint64_t *addr);
	/*
	 * Presents a frame and watches the front end: bits 11:8 of its control register are 6
	 * when no flush is pending (the Linux driver sends its flip event on that), and the
	 * three interrupt flags are counted. Writes what it saw to `text`.
	 */
	void flushTrace(char *text, size_t size);
	/* One line of register state for the log and the sysctl. */
	void describe(char *text, size_t size);
};

#endif
