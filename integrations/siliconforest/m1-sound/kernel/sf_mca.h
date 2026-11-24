/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The Apple audio transport of T8103: an NCO clock channel, one MCA cluster (frame sync
 * generator and transmit serialiser) routed to an I2S port, and one ADMAC transmit
 * channel feeding it from memory behind dart-sio.
 *
 * SFMca is one playback stream: a front-end cluster that provides the clocks (64 or 256
 * bit clocks per frame, 32-bit slots, I2S) to one or more I2S ports. SFMcaCapture is a
 * capture stream on another cluster that takes its clocks from one of those ports
 * (the speakers' current/voltage sense). Callers serialise the calls on each object;
 * nothing here runs in interrupt context: the ADMAC's rings are polled.
 */
#ifndef _SF_MCA_H
#define _SF_MCA_H

#include "sf_soc.h"

struct SFMcaStatus {
	uint32_t ncoCtrl, ncoDiv;
	uint32_t mclkStatus, syncgenStatus, serdesStatus, serdesConf;
	uint32_t portEnables, portClockSel, portDataSel;
	uint32_t mcaIntState;
	uint32_t admacTxEnabled, descRing, reportRing, chanStatus, residue;
};

/* One ADMAC channel: even numbers transmit, odd ones receive. */
struct SFAdmacChan {
	const SFMmio *admac;
	unsigned      ch;

	SFAdmacChan() : admac(NULL), ch(0) {}
	/* Stops the channel and sets it up: `wordBytes` 2 or 4, `frameWords` 2 or 4, its block of the SRAM. */
	void configure(unsigned wordBytes, unsigned frameWords, unsigned sramBlock);
	void resetRings(void);
	bool descRingFull(void) const;
	bool descRingEmpty(void) const;
	void submit(uint64_t iova, uint32_t bytes);
	void start(void);
	void stop(void);
	unsigned drainReports(uint64_t *counter);
	bool clearError(void);
	bool enabled(void) const;
};

struct SFMca {
	SFMmio   nco, mca, sw, admac;
	SFAdmacChan tx;
	uint32_t ncoRefHz;
	unsigned fe;            /* cluster whose serialiser and clocks are used */
	unsigned ports;         /* bit n: the I2S pins of cluster n are driven */
	unsigned slots;         /* 32-bit slots per frame: 2 or 8 */
	unsigned channels;      /* slots that carry a channel: 2 or 4 */
	unsigned sramBlock;
	bool     attached, clocksOn, running;

	SFMca() : ncoRefHz(0), fe(0), ports(0), slots(2), channels(2), sramBlock(0), attached(false), clocksOn(false),
	    running(false) {}

	/*
	 * Powers the audio blocks (resetting them the first time any stream attaches) and
	 * maps them. `sram`: which 2 KiB block of the DMA controller's transmit SRAM this
	 * stream uses; each stream needs its own. 0 or errno.
	 */
	int attach(unsigned feCluster, unsigned portMask, unsigned slotsPerFrame, unsigned channelCount, unsigned sram);
	/*
	 * Bit clock = rate * 32 * slots on the ports, frame sync at `rate`, serialiser
	 * configured and idle. From here the codecs see their clocks.
	 */
	int startClocks(uint32_t rate);
	/* Resets the serialiser and the DMA rings; nothing moves until descriptors are queued. */
	void prepareDma(void);
	bool descRingFull(void) const { return tx.descRingFull(); }
	bool descRingEmpty(void) const { return tx.descRingEmpty(); }
	/* Queues [iova, iova + bytes) for transmission. */
	void submit(uint64_t iova, uint32_t bytes) { tx.submit(iova, bytes); }
	/* Starts the DMA channel (at least one descriptor must be queued), then the serialiser. */
	void startDma(void);
	/*
	 * Takes the completion reports that are waiting. Returns their number; *counter
	 * (optional) is the 24 MHz timestamp of the last one.
	 */
	unsigned drainReports(uint64_t *counter) { return tx.drainReports(counter); }
	/* Clears a latched ring error; true when there was one. */
	bool clearError(void) { return tx.clearError(); }
	void stopDma(void);
	void stopClocks(void);
	void status(SFMcaStatus *out) const;
};

/*
 * Capture on a cluster's second receiver, data from the I2S ports in `ports`. Either the
 * cluster is a clock consumer (`consumer`: its frame comes from the port `syncPort`,
 * which a SFMca on another cluster drives; the speakers' sense), or it is the cluster of
 * a running SFMca and shares its clocks and frame (the headset microphone).
 */
struct SFMcaCapture {
	SFMmio   mca, sw, admac;
	SFAdmacChan rx;
	unsigned cluster, ports, syncPort, slots, slotBits, channels, sramBlock;
	bool     consumer, attached, running;

	SFMcaCapture() : cluster(0), ports(0), syncPort(0), slots(16), slotBits(16), channels(8), sramBlock(0),
	    consumer(true), attached(false), running(false) {}

	/*
	 * `slotWidth` 16 or 32 bits, `channelCount` leading slots captured (samples of that
	 * width in memory), `sram`: this stream's block of the receive SRAM.
	 */
	int attach(unsigned captureCluster, unsigned portMask, bool clockConsumer, unsigned syncFromPort,
	    unsigned slotsPerFrame, unsigned slotWidth, unsigned channelCount, unsigned sram);
	/* Configures the receiver (the playback stream's clocks must run). */
	int prepare(void);
	bool descRingFull(void) const { return rx.descRingFull(); }
	void submit(uint64_t iova, uint32_t bytes) { rx.submit(iova, bytes); }
	void start(void);
	unsigned drainReports(uint64_t *counter) { return rx.drainReports(counter); }
	bool clearError(void) { return rx.clearError(); }
	void stop(void);
	/* serialiser status, configuration, sync generator status */
	void status(uint32_t out[3]) const;
};

#endif
