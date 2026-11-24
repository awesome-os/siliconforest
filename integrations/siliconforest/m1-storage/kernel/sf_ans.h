/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The M1's internal storage controller (ANS2: an RTKit coprocessor that speaks NVMe
 * through a linear submission queue and the NVMMU), READ-ONLY.
 *
 * Ported from the Omacom Linux tree (omacom/linux-aurora, integration/aurora):
 *   drivers/nvme/host/apple.c   controller bring-up, queues, NVMMU, shutdown ("t8103" path)
 *   drivers/nvme/host/core.c    nvme_enable_ctrl / nvme_disable_ctrl
 *   drivers/soc/apple/rtkit.c   RTKit boot and shutdown handshakes
 *   drivers/soc/apple/sart.c    SART v2 allow-list entries
 * and checked against m1n1 src/nvme.c. Nothing is sent that the Linux driver does not
 * send on every boot, and less: no Set Features, no flush, no write of any kind.
 *
 * One thread owns a SFAns after construction: the engine thread of SFAppleNVMe.cpp.
 * Nothing here takes a lock or runs in interrupt context; the completion queues and the
 * mailbox are polled. Data is read into buffers this object owns (bounce buffers), so a
 * device DMA can only ever land in memory of this driver.
 */
#ifndef _SF_ANS_H
#define _SF_ANS_H

#include "sf_nvme_gate.h"
#include "sf_soc.h"
#include <IOKit/IOBufferMemoryDescriptor.h>

#define SF_ANS_IO_DEPTH     64          /* apple_nvme_t8103_hw.max_queue_depth */
#define SF_ANS_AQ_DEPTH     2           /* APPLE_NVME_AQ_DEPTH */
#define SF_ANS_SLOTS        8           /* reads in flight; tags SF_ANS_AQ_DEPTH .. */
#define SF_ANS_SLOT_BYTES   (SF_NVME_MAX_READ_BLOCKS * 4096u)
#define SF_ANS_SART_ENTRIES 16
#define SF_ANS_SHMEM        6

struct SFAnsMem {
	IOBufferMemoryDescriptor *md;
	uint8_t                  *va;
	uint64_t                  phys;
	uint32_t                  size;

	SFAnsMem() : md(NULL), va(NULL), phys(0), size(0) {}
	/* Wired, zeroed, physically contiguous, 16 KiB aligned. Never freed while the firmware may run. */
	bool alloc(uint32_t bytes);
};

struct SFAnsSlot {
	SFAnsMem  data;                 /* SF_ANS_SLOT_BYTES */
	SFAnsMem  prp;                  /* one page: the PRP list of this slot's data (fixed) */
	bool      busy;
	uint64_t  submitted;            /* now_us() */
	void     *cookie;               /* the caller's request */
	uint32_t  blocks;
};

struct SFAnsCompletion {
	unsigned  slot;
	uint16_t  status;               /* NVMe status field without the phase bit; 0: success */
	void     *cookie;
	uint64_t  micros;               /* from the doorbell to the completion being seen */
};

struct SFAns {
	SFMmio    asc;                  /* /arm-io/ans reg 0: coprocessor control, mailbox at +0x8000 */
	SFMmio    nvme;                 /* /arm-io/ans reg 3: NVMe, linear SQ and NVMMU registers */
	SFMmio    sart;                 /* /arm-io/sart-ans */
	bool      verbose;

	/* SART */
	uint32_t  sartProtected;        /* entries the boot loader left: never written */
	uint32_t  sartUsed;             /* entries this driver wrote */
	uint32_t  sartVersion;

	/* RTKit */
	uint32_t  endpoints[8];
	uint32_t  version;
	bool      helloSeen, epmapDone, bootFailed, crashed, iopAck, apAck, cpuStarted;
	uint32_t  iopPower, apPower;
	SFAnsMem  shmem[SF_ANS_SHMEM];  /* buffers the firmware asked for (syslog, crashlog, ioreport, oslog) */
	uint8_t   shmemEp[SF_ANS_SHMEM];
	unsigned  nshmem;
	uint32_t  syslogEntries, syslogMsgSize, syslogLines, rxMessages, txMessages, unknownMessages;

	/* NVMe */
	SFAnsMem  asq, acq, atcb, iosq, iocq, iotcb, adminData;
	uint32_t  acqHead, acqPhase, iocqHead, iocqPhase;
	uint64_t  cap;
	uint32_t  cc;                   /* the driver's copy of CC (nvme_ctrl.ctrl_config) */
	uint32_t  vs;
	bool      enabled, ioQueues, dead;
	uint8_t   idCtrl[4096], idNs[4096], smart[512];
	uint64_t  nsBlocks;             /* namespace 1 */
	uint32_t  blockSize;
	SFAnsSlot slot[SF_ANS_SLOTS];
	unsigned  inflight;

	/* Counters. gateRejected is the one the tests assert is 0. */
	uint32_t  gateRejected, gateLastReason, gateLastOpcode, gateLastQueue;
	uint64_t  adminCommands, readCommands, readBlocks, cmdErrors, cmdTimeouts, tcbInvalFailures, strayCompletions;
	uint64_t  doorbells[2];         /* submissions per queue */
	uint64_t  opcodeCount[2][8];    /* submissions per queue and opcode (all allowed opcodes are < 8) */

	/*
	 * Registers and power states as found, without changing anything: PMGR states are
	 * always read; device registers only when the ANS2 domain is already on.
	 */
	void observe(void);
	/*
	 * Power domains, mappings, memory, coprocessor reset and start, RTKit boot, controller
	 * enable, Identify (controller, namespace 1), I/O queue pair, SMART log. 0 or an errno
	 * value; `why` receives a short reason. After a failure call stop().
	 */
	int start(char *why, size_t whyLen);
	/* The slot a read may use, or -1. */
	int freeSlot(void) const;
	/* Starts a read of `blocks` blocks at `lba` into slot `s`. 0 or an errno value. */
	int submitRead(unsigned s, uint64_t lba, uint32_t blocks, void *cookie);
	/* Takes finished reads off the I/O completion queue; at most `max`. Also serves the mailbox. */
	unsigned reap(SFAnsCompletion *out, unsigned max);
	/* Reads that have been in flight for longer than `micros`; marks the controller dead when there are. */
	unsigned overdue(uint64_t micros);
	/* Refreshes smart[]. */
	int readSmart(void);
	/*
	 * apple_nvme_shutdown(): deletes the I/O queues, CC.SHN normal shutdown, CC.EN = 0,
	 * RTKit AP quiesced / IOP sleep, coprocessor stopped, SART entries of this driver
	 * cleared. Safe to call in any state. Returns true when every step was acknowledged.
	 */
	bool stop(char *why, size_t whyLen);

	/* internal */
	bool mboxRecv(uint64_t *msg, uint8_t *ep);
	bool mboxSend(uint8_t ep, uint64_t msg);
	bool mgmtSend(uint8_t type, uint64_t msg);
	bool startEp(uint8_t ep);
	bool waitFlag(const bool *flag, uint32_t ms);
	unsigned poll(void);
	void rxMgmt(uint64_t msg);
	void rxBuffer(uint8_t ep, uint64_t msg, const char *what);
	void rxSyslog(uint64_t msg);
	void rxIoreport(uint64_t msg);
	void rxOslog(uint64_t msg);
	void rxCrashlog(uint64_t msg);
	bool hasEndpoint(unsigned n) const { return n < 256 && (endpoints[n / 32] & (1u << (n % 32))) != 0; }
	bool rtkitRunning(void) const { return !crashed && (iopPower & 0xff) == 0x20 && (apPower & 0xff) == 0x20; }
	void sartScan(void);
	int sartAdd(uint64_t phys, uint32_t size);
	void sartClear(void);
	bool waitCsts(uint32_t mask, uint32_t value, uint32_t ms, const char *what);
	bool ring(unsigned queue, unsigned tag, const uint8_t sqe[64], bool dataIn);
	int admin(const uint8_t sqe[64], bool dataIn, uint32_t timeoutMs);
	int identify(uint32_t cns, uint32_t nsid, uint8_t *out);
	bool rtkitPower(bool ap, uint32_t state);
};

uint64_t sf_ans_now_us(void);

#endif
