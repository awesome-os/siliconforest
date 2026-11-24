// SPDX-License-Identifier: GPL-2.0
/*
 * The M1's internal storage controller, read-only. See sf_ans.h.
 *
 * Ported from the Omacom Linux tree (omacom/linux-aurora, integration/aurora, 6a08a08e):
 *   drivers/nvme/host/apple.c  "Apple ANS NVM Express device driver", Copyright The Asahi
 *                              Linux Contributors, based on pci.c, Copyright (c) 2011-2014
 *                              Intel Corporation, and rdma.c, Copyright (c) 2015-2016 HGST
 *                              (GPL-2.0): apple_nvme_reset_work, apple_nvme_submit_cmd_t8103,
 *                              apple_nvme_poll_cq, apple_nvme_create_cq/sq,
 *                              apple_nvme_disable_legacy, apple_nvme_shutdown
 *   drivers/nvme/host/core.c   nvme_enable_ctrl, nvme_disable_ctrl, nvme_wait_ready (GPL-2.0)
 *   drivers/soc/apple/rtkit.c  "Apple RTKit IPC library" (GPL-2.0-only OR MIT)
 *   drivers/soc/apple/mailbox.c ASC mailbox registers (GPL-2.0-only OR MIT)
 *   drivers/soc/apple/sart.c   "Apple SART device driver" (GPL-2.0-only OR MIT)
 * The RTKit client is the polled one of ../../m1-sound/aop/kernel/sf_aop.cpp without its
 * application endpoints.
 *
 * Differences from Linux, by design:
 *   - no write path: no Write, Flush, Dataset Management, Set Features or any other
 *     command outside sf_nvme_gate.c's allowlist exists here, and every submission
 *     passes that gate (twice: the command as built, and the bytes in the queue);
 *   - polled: one thread, no interrupt handler, no locks;
 *   - reads land in buffers of this driver and are copied out by the caller;
 *   - a coprocessor found running is left alone instead of woken (apple_rtkit_wake()):
 *     the boot loader hands the ANS over stopped, and anything else is unexpected.
 */
#include "sf_ans.h"

#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <libkern/OSByteOrder.h>
#include <string.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFAns: " fmt "\n", ##__VA_ARGS__)
#define VLOG(fmt, ...) do { if (verbose) IOLog("SFAns: " fmt "\n", ##__VA_ARGS__); } while (0)

/* ---- apple.c ---- */
#define ANS_COPROC_CPU_CONTROL     0x44
#define ANS_COPROC_CPU_CONTROL_RUN (1u << 4)
#define ANS_ACQ_DB                 0x1004
#define ANS_IOCQ_DB                0x100c
#define ANS_MAX_PEND_CMDS_CTRL     0x1210
#define ANS_BOOT_STATUS            0x1300
#define ANS_BOOT_STATUS_OK         0xde71ce55u
#define ANS_LINEAR_SQ_CTRL         0x24908
#define ANS_LINEAR_SQ_EN           (1u << 0)
#define ANS_LINEAR_ASQ_DB          0x2490c
#define ANS_LINEAR_IOSQ_DB         0x24910
#define NVMMU_NUM_TCBS             0x28100
#define NVMMU_ASQ_TCB_BASE         0x28108
#define NVMMU_IOSQ_TCB_BASE        0x28110
#define NVMMU_TCB_INVAL            0x28118
#define NVMMU_TCB_STAT             0x28120
#define ANS_BOOT_TIMEOUT_MS        1000
#define ANS_TCB_DMA_FROM_DEVICE    (1u << 0)
#define ANS_TCB_DMA_TO_DEVICE      (1u << 1)
/* struct apple_nvmmu_tcb by offset (128 bytes) */
#define TCB_SIZE       128
#define TCB_OPCODE     0
#define TCB_DMA_FLAGS  1
#define TCB_COMMAND_ID 2
#define TCB_LENGTH     4
#define TCB_PRP1       24
#define TCB_PRP2       32

/* ---- include/linux/nvme.h ---- */
#define NVME_REG_CAP   0x00
#define NVME_REG_VS    0x08
#define NVME_REG_CC    0x14
#define NVME_REG_CSTS  0x1c
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30
#define NVME_REG_CRTO  0x68
#define NVME_CC_ENABLE      (1u << 0)
#define NVME_CC_CSS_NVM     (0u << 4)
#define NVME_CC_CSS_CSI     (6u << 4)
#define NVME_CC_MPS_SHIFT   7
#define NVME_CC_AMS_RR      (0u << 11)
#define NVME_CC_SHN_NONE    (0u << 14)
#define NVME_CC_SHN_NORMAL  (1u << 14)
#define NVME_CC_SHN_MASK    (3u << 14)
#define NVME_CC_IOSQES      (6u << 16)
#define NVME_CC_IOCQES      (4u << 20)
#define NVME_CC_CRIME       (1u << 24)
#define NVME_CSTS_RDY       (1u << 0)
#define NVME_CSTS_CFS       (1u << 1)
#define NVME_CSTS_SHST_MASK (3u << 2)
#define NVME_CSTS_SHST_CMPLT (2u << 2)
#define NVME_CAP_TIMEOUT(cap) ((uint32_t)(((cap) >> 24) & 0xff))
#define NVME_CAP_MPSMIN(cap)  ((uint32_t)(((cap) >> 48) & 0xf))
#define NVME_CAP_CSS(cap)     ((uint32_t)(((cap) >> 37) & 0xff))
#define NVME_CAP_CSS_CSI      (1u << 6)
#define NVME_CAP_CRMS_CRWMS   (1ULL << 59)
#define NVME_CTRL_PAGE_SHIFT  12
#define NVME_QUEUE_PHYS_CONTIG (1u << 0)
#define NVME_CQ_IRQ_ENABLED    (1u << 1)
/* struct nvme_command / nvme_completion by offset */
#define SQE_SIZE   64
#define SQE_CID    2
#define SQE_NSID   4
#define SQE_PRP1   24
#define SQE_PRP2   32
#define SQE_CDW10  40
#define SQE_CDW11  44
#define SQE_CDW12  48
#define CQE_SIZE   16
#define CQE_CID    12
#define CQE_STATUS 14
#define ADMIN_TIMEOUT_MS 5000

/* ---- rtkit.c, mailbox.c ---- */
#define ASC_MBOX             0x8000         /* ans_mbox of the Linux device tree: the ASC registers + 0x8000 */
#define MBOX_CONTROL_FULL    (1u << 16)
#define MBOX_CONTROL_EMPTY   (1u << 17)
#define MBOX_A2I_CONTROL     0x110
#define MBOX_A2I_SEND0       0x800
#define MBOX_A2I_SEND1       0x808
#define MBOX_I2A_CONTROL     0x114
#define MBOX_I2A_RECV0       0x830
#define MBOX_I2A_RECV1       0x838
#define MBOX_TX_TIMEOUT_MS   500
#define PWR_STATE_SLEEP      0x01
#define PWR_STATE_QUIESCED   0x10
#define PWR_STATE_ON         0x20
#define EP_MGMT              0
#define EP_CRASHLOG          1
#define EP_SYSLOG            2
#define EP_DEBUG             3
#define EP_IOREPORT          4
#define EP_OSLOG             8
#define EP_TRACEKIT          0xa
#define APP_ENDPOINT_START   0x20
#define MGMT_TYPE(m)         ((unsigned)(((m) >> 52) & 0xff))
#define MGMT_HELLO           1
#define MGMT_HELLO_REPLY     2
#define MGMT_STARTEP         5
#define MGMT_SET_IOP_PWR_STATE     6
#define MGMT_SET_IOP_PWR_STATE_ACK 7
#define MGMT_EPMAP           8
#define MGMT_EPMAP_REPLY     8
#define MGMT_SET_AP_PWR_STATE      0xb
#define MGMT_SET_AP_PWR_STATE_ACK  0xb
#define MGMT_EPMAP_LAST      (1ULL << 51)
#define MGMT_EPMAP_REPLY_MORE (1ULL << 0)
#define MGMT_STARTEP_FLAG    (1ULL << 1)
#define BUFFER_REQUEST       1
#define IOREPORT_UNKNOWN1    0x8
#define IOREPORT_UNKNOWN2    0xc
#define SYSLOG_LOG           5
#define SYSLOG_INIT          8
#define OSLOG_BUFFER_REQUEST 1
#define OSLOG_LOG            2
#define RTKIT_MIN_VERSION    11
#define RTKIT_MAX_VERSION    12
#define RTKIT_WAIT_MS        3000

/* ---- sart.c, SART v2 ---- */
#define SART2_CONFIG(i)      (0x00 + 4 * (i))
#define SART2_PADDR(i)       (0x40 + 4 * (i))
#define SART2_FLAGS_ALLOW    0xffu
#define SART2_SIZE_MAX       0xffffffu

static inline void mem_sync(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void p32(uint8_t *p, uint32_t v) { OSWriteLittleInt32(p, 0, v); }
static inline void p64(uint8_t *p, uint64_t v) { OSWriteLittleInt64(p, 0, v); }
static inline uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t g32(const uint8_t *p) { return OSReadLittleInt32(p, 0); }
static inline uint64_t g64(const uint8_t *p) { return OSReadLittleInt64(p, 0); }

uint64_t
sf_ans_now_us(void)
{
	uint64_t abs, ns;
	clock_get_uptime(&abs);
	absolutetime_to_nanoseconds(abs, &ns);
	return ns / 1000ULL;
}

static uint64_t now_ms(void) { return sf_ans_now_us() / 1000ULL; }

bool
SFAnsMem::alloc(uint32_t bytes)
{
	bytes = (bytes + 0x3fffu) & ~0x3fffu;
	if (md != NULL && va != NULL && size == bytes) {
		/* a second start in the same boot reuses the memory of the first */
		bzero(va, bytes);
		return true;
	}
	md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
	    bytes, 0x0000000fffffc000ULL);
	if (md == NULL)
		return false;
	md->prepare();
	va = (uint8_t *)md->getBytesNoCopy();
	phys = md->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
	size = bytes;
	if (va == NULL || phys == 0 || (phys & 0x3fff))
		return false;
	bzero(va, bytes);
	return true;
}

/* ---- mailbox and RTKit system endpoints ---------------------------------------------- */

bool
SFAns::mboxRecv(uint64_t *msg, uint8_t *epOut)
{
	volatile uint8_t *mb = asc.va + ASC_MBOX;

	if (OSReadLittleInt32(mb, MBOX_I2A_CONTROL) & MBOX_CONTROL_EMPTY)
		return false;
	uint64_t m0 = OSReadLittleInt64(mb, MBOX_I2A_RECV0);
	uint64_t m1 = OSReadLittleInt64(mb, MBOX_I2A_RECV1);
	OSSynchronizeIO();
	mem_sync();
	*msg = m0;
	*epOut = (uint8_t)m1;
	rxMessages++;
	return true;
}

bool
SFAns::mboxSend(uint8_t epNo, uint64_t msg)
{
	volatile uint8_t *mb = asc.va + ASC_MBOX;
	uint64_t deadline = now_ms() + MBOX_TX_TIMEOUT_MS;

	if (crashed) {
		LOG("the firmware has crashed; 0x%016llx to endpoint 0x%02x not sent", msg, epNo);
		return false;
	}
	while (OSReadLittleInt32(mb, MBOX_A2I_CONTROL) & MBOX_CONTROL_FULL) {
		if (now_ms() > deadline) {
			LOG("mailbox full for %u ms; 0x%016llx to endpoint 0x%02x not sent", MBOX_TX_TIMEOUT_MS, msg, epNo);
			return false;
		}
		IOSleep(1);
	}
	mem_sync();
	VLOG("> ep 0x%02x 0x%016llx", epNo, msg);
	OSWriteLittleInt64(mb, MBOX_A2I_SEND0, msg);
	OSWriteLittleInt64(mb, MBOX_A2I_SEND1, (uint64_t)epNo);
	OSSynchronizeIO();
	txMessages++;
	return true;
}

bool
SFAns::mgmtSend(uint8_t type, uint64_t msg)
{
	msg &= ~(0xffULL << 52);
	msg |= (uint64_t)type << 52;
	return mboxSend(EP_MGMT, msg);
}

bool
SFAns::startEp(uint8_t epNo)
{
	if (!hasEndpoint(epNo))
		return false;
	return mgmtSend(MGMT_STARTEP, ((uint64_t)epNo << 32) | MGMT_STARTEP_FLAG);
}

bool
SFAns::waitFlag(const bool *flag, uint32_t ms)
{
	uint64_t deadline = now_ms() + ms;

	for (;;) {
		poll();
		if (*flag)
			return true;
		if (bootFailed || crashed || now_ms() > deadline)
			return false;
		IOSleep(1);
	}
}

void
SFAns::rxMgmt(uint64_t msg)
{
	switch (MGMT_TYPE(msg)) {
	case MGMT_HELLO: {
		unsigned minVer = (unsigned)(msg & 0xffff), maxVer = (unsigned)((msg >> 16) & 0xffff);
		unsigned want = maxVer < RTKIT_MAX_VERSION ? maxVer : RTKIT_MAX_VERSION;

		if (minVer > RTKIT_MAX_VERSION || maxVer < RTKIT_MIN_VERSION) {
			LOG("RTKit: firmware protocol versions %u..%u are not supported", minVer, maxVer);
			bootFailed = true;
			return;
		}
		LOG("RTKit: HELLO, versions %u..%u; protocol version %u", minVer, maxVer, want);
		version = want;
		helloSeen = true;
		mgmtSend(MGMT_HELLO_REPLY, (uint64_t)want | ((uint64_t)want << 16));
		break;
	}
	case MGMT_EPMAP: {
		uint32_t bitmap = (uint32_t)msg, base = (uint32_t)((msg >> 32) & 7);
		uint64_t reply = (uint64_t)base << 32;

		endpoints[base] |= bitmap;
		LOG("RTKit: endpoint map base %u bitmap 0x%08x%s", base, bitmap, (msg & MGMT_EPMAP_LAST) ? " (last)" : "");
		reply |= (msg & MGMT_EPMAP_LAST) ? MGMT_EPMAP_LAST : MGMT_EPMAP_REPLY_MORE;
		mgmtSend(MGMT_EPMAP_REPLY, reply);
		if (!(msg & MGMT_EPMAP_LAST))
			return;
		for (unsigned e = 1; e < APP_ENDPOINT_START; e++) {
			if (!hasEndpoint(e))
				continue;
			switch (e) {
			case EP_SYSLOG: case EP_CRASHLOG: case EP_DEBUG: case EP_IOREPORT: case EP_OSLOG: case EP_TRACEKIT:
				if (!startEp((uint8_t)e)) {
					LOG("RTKit: system endpoint 0x%02x could not be started", e);
					bootFailed = true;
					return;
				}
				break;
			default:
				LOG("RTKit: unknown system endpoint 0x%02x (not started)", e);
			}
		}
		epmapDone = true;
		break;
	}
	case MGMT_SET_IOP_PWR_STATE_ACK:
		LOG("RTKit: IOP power state 0x%x -> 0x%x", iopPower, (unsigned)(msg & 0xffff));
		iopPower = (uint32_t)(msg & 0xffff);
		iopAck = true;
		break;
	case MGMT_SET_AP_PWR_STATE_ACK:
		LOG("RTKit: AP power state 0x%x -> 0x%x", apPower, (unsigned)(msg & 0xffff));
		apPower = (uint32_t)(msg & 0xffff);
		apAck = true;
		break;
	default:
		LOG("RTKit: unknown management message 0x%016llx", msg);
		unknownMessages++;
	}
}

/*
 * apple_rtkit_common_rx_get_buffer() with apple_nvme_sart_dma_setup(): memory of this
 * driver, allowed in the SART, named to the firmware by its physical address.
 */
void
SFAns::rxBuffer(uint8_t epNo, uint64_t msg, const char *what)
{
	uint64_t wantSize, wantIova, reply;
	SFAnsMem *m = NULL;

	if (epNo == EP_OSLOG) {
		wantSize = (msg >> 36) & 0xfffff;
		wantIova = (msg & 0xfffffffffULL) << 12;
	} else {
		wantSize = ((msg >> 44) & 0xff) << 12;
		wantIova = msg & 0xfffffffffffULL;
	}
	if (wantSize == 0 || wantSize > (16u << 20)) {
		LOG("RTKit: %s buffer request of 0x%llx bytes (0x%016llx); not answered", what, wantSize, msg);
		return;
	}
	for (unsigned i = 0; i < nshmem; i++)
		if (shmemEp[i] == epNo)
			m = &shmem[i];
	if (m != NULL) {
		/* a repeated request: answered again when identical, refused when changed */
		if (wantSize != m->size || (wantIova != m->phys && wantIova != 0)) {
			LOG("RTKit: %s buffer request for 0x%llx bytes at 0x%llx while 0x%x bytes at 0x%llx are live; not answered",
			    what, wantSize, wantIova, m->size, m->phys);
			return;
		}
	} else {
		if (wantIova != 0) {
			/* Linux: only a buffer inherited from the boot loader may name its address (post-M4) */
			LOG("RTKit: %s buffer request names its own address 0x%llx; not answered", what, wantIova);
			return;
		}
		if (nshmem >= SF_ANS_SHMEM) {
			LOG("RTKit: no room for another shared buffer (%s); not answered", what);
			return;
		}
		m = &shmem[nshmem];
		if (!m->alloc((uint32_t)wantSize)) {
			LOG("RTKit: no memory for the %s buffer of 0x%llx bytes; not answered", what, wantSize);
			return;
		}
		/* the firmware asked for wantSize; the allocation is rounded to 16 KiB */
		m->size = (uint32_t)wantSize;
		if (sartAdd(m->phys, (m->size + 0xfffu) & ~0xfffu) != 0) {
			LOG("RTKit: the %s buffer could not be allowed in the SART; not answered", what);
			return;
		}
		shmemEp[nshmem++] = epNo;
		LOG("RTKit: %s buffer: 0x%x bytes at 0x%llx", what, m->size, m->phys);
	}
	if (epNo == EP_OSLOG)
		reply = ((uint64_t)OSLOG_BUFFER_REQUEST << 56) | ((uint64_t)m->size << 36) | (m->phys >> 12);
	else
		reply = ((uint64_t)BUFFER_REQUEST << 52) | ((uint64_t)(m->size >> 12) << 44) | m->phys;
	mboxSend(epNo, reply);
}

void
SFAns::rxSyslog(uint64_t msg)
{
	switch (MGMT_TYPE(msg)) {
	case BUFFER_REQUEST:
		rxBuffer(EP_SYSLOG, msg, "syslog");
		break;
	case SYSLOG_INIT:
		syslogEntries = (uint32_t)(msg & 0xff);
		syslogMsgSize = (uint32_t)((msg >> 24) & 0xff);
		break;
	case SYSLOG_LOG: {
		uint32_t idx = (uint32_t)(msg & 0xff), entry = 0x20 + syslogMsgSize;
		const SFAnsMem *m = NULL;
		char context[24], text[256];

		for (unsigned i = 0; i < nshmem; i++)
			if (shmemEp[i] == EP_SYSLOG)
				m = &shmem[i];
		if (m != NULL && syslogMsgSize != 0 && idx < syslogEntries && (idx + 1) * entry <= m->size) {
			uint32_t n = syslogMsgSize < sizeof(text) - 1 ? syslogMsgSize : (uint32_t)sizeof(text) - 1;
			memcpy(context, m->va + idx * entry + 8, sizeof(context));
			context[sizeof(context) - 1] = 0;
			memcpy(text, m->va + idx * entry + 8 + sizeof(context), n);
			text[n] = 0;
			n = (uint32_t)strlen(text);
			while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' '))
				text[--n] = 0;
			for (uint32_t i = 0; i < n; i++)
				if (text[i] < 0x20 || text[i] > 0x7e)
					text[i] = '.';
			syslogLines++;
			LOG("syslog: %s: %s", context, text);
		}
		/* the record is acknowledged whether it could be read or not */
		mboxSend(EP_SYSLOG, msg);
		break;
	}
	default:
		LOG("RTKit: unknown syslog message 0x%016llx", msg);
		unknownMessages++;
	}
}

void
SFAns::rxIoreport(uint64_t msg)
{
	switch (MGMT_TYPE(msg)) {
	case BUFFER_REQUEST:
		rxBuffer(EP_IOREPORT, msg, "ioreport");
		break;
	case IOREPORT_UNKNOWN1:
	case IOREPORT_UNKNOWN2:
		/* must be acknowledged or the coprocessor hangs */
		mboxSend(EP_IOREPORT, msg);
		break;
	default:
		LOG("RTKit: unknown ioreport message 0x%016llx", msg);
		unknownMessages++;
	}
}

void
SFAns::rxOslog(uint64_t msg)
{
	unsigned type = (unsigned)(msg >> 56);

	switch (type) {
	case OSLOG_BUFFER_REQUEST:
		rxBuffer(EP_OSLOG, msg, "oslog");
		break;
	case OSLOG_LOG:
		mboxSend(EP_OSLOG, msg);
		break;
	case 3: case 4: case 5:
		break;
	default:
		LOG("RTKit: unknown oslog message 0x%016llx", msg);
		unknownMessages++;
	}
}

void
SFAns::rxCrashlog(uint64_t msg)
{
	bool have = false;

	for (unsigned i = 0; i < nshmem; i++)
		if (shmemEp[i] == EP_CRASHLOG)
			have = true;
	if (MGMT_TYPE(msg) != BUFFER_REQUEST) {
		LOG("RTKit: unknown crashlog message 0x%016llx", msg);
		unknownMessages++;
		return;
	}
	if (!have) {
		rxBuffer(EP_CRASHLOG, msg, "crashlog");
		return;
	}
	/* the same message once the buffer exists: the firmware has crashed */
	crashed = true;
	dead = true;
	LOG("RTKit: THE ANS FIRMWARE CRASHED (0x%016llx); the controller is not used any more", msg);
}

unsigned
SFAns::poll(void)
{
	unsigned n = 0;
	uint64_t msg;
	uint8_t epNo;

	if (asc.va == NULL || !cpuStarted)
		return 0;
	while (n < 256 && mboxRecv(&msg, &epNo)) {
		n++;
		VLOG("< ep 0x%02x 0x%016llx", epNo, msg);
		switch (epNo) {
		case EP_MGMT:     rxMgmt(msg); break;
		case EP_CRASHLOG: rxCrashlog(msg); break;
		case EP_SYSLOG:   rxSyslog(msg); break;
		case EP_IOREPORT: rxIoreport(msg); break;
		case EP_OSLOG:    rxOslog(msg); break;
		default:
			LOG("RTKit: message 0x%016llx on unexpected endpoint 0x%02x", msg, epNo);
			unknownMessages++;
		}
	}
	return n;
}

/* apple_rtkit_set_ap_power_state() / apple_rtkit_set_iop_power_state() */
bool
SFAns::rtkitPower(bool ap, uint32_t state)
{
	bool *ack = ap ? &apAck : &iopAck;

	*ack = false;
	if (!mgmtSend(ap ? MGMT_SET_AP_PWR_STATE : MGMT_SET_IOP_PWR_STATE, state))
		return false;
	if (!waitFlag(ack, RTKIT_WAIT_MS)) {
		LOG("RTKit: no acknowledgment of %s power state 0x%x", ap ? "AP" : "IOP", state);
		return false;
	}
	return (ap ? apPower : iopPower) == state;
}

/* ---- SART (v2) ----------------------------------------------------------------------- */

/* apple_sart_probe(): entries with flags are the boot loader's and are never written. */
void
SFAns::sartScan(void)
{
	sartProtected = 0;
	for (unsigned i = 0; i < SF_ANS_SART_ENTRIES; i++) {
		uint32_t cfg = sart.r32(SART2_CONFIG(i)), paddr = sart.r32(SART2_PADDR(i));
		if ((cfg >> 24) == 0)
			continue;
		sartProtected |= 1u << i;
		LOG("SART: boot loader entry %u: flags 0x%02x, 0x%llx bytes at 0x%llx", i, cfg >> 24,
		    (uint64_t)(cfg & 0xffffff) << 12, (uint64_t)paddr << 12);
	}
}

/* apple_sart_add_allowed_region() */
int
SFAns::sartAdd(uint64_t phys, uint32_t size)
{
	if ((phys & 0xfff) || (size & 0xfff) || (size >> 12) > SART2_SIZE_MAX || (phys >> 12) > 0xffffffffULL)
		return EINVAL;
	for (unsigned i = 0; i < SF_ANS_SART_ENTRIES; i++) {
		if ((sartProtected | sartUsed) & (1u << i))
			continue;
		sartUsed |= 1u << i;
		sart.w32(SART2_PADDR(i), (uint32_t)(phys >> 12));
		sart.w32(SART2_CONFIG(i), (SART2_FLAGS_ALLOW << 24) | (size >> 12));
		VLOG("SART: entry %u: 0x%x bytes at 0x%llx", i, size, phys);
		return 0;
	}
	LOG("SART: no free entry for 0x%x bytes at 0x%llx", size, phys);
	return EBUSY;
}

/* apple_sart_shutdown(): every entry that is not the boot loader's is cleared. */
void
SFAns::sartClear(void)
{
	if (sart.va == NULL)
		return;
	for (unsigned i = 0; i < SF_ANS_SART_ENTRIES; i++) {
		if (sartProtected & (1u << i))
			continue;
		sart.w32(SART2_PADDR(i), 0);
		sart.w32(SART2_CONFIG(i), 0);
	}
	sartUsed = 0;
}

/* ---- NVMe ---------------------------------------------------------------------------- */

/* nvme_wait_ready() */
bool
SFAns::waitCsts(uint32_t mask, uint32_t value, uint32_t ms, const char *what)
{
	uint64_t deadline = now_ms() + ms;
	uint32_t csts;

	for (;;) {
		csts = nvme.r32(NVME_REG_CSTS);
		if (csts == 0xffffffffu)
			break;
		if ((csts & mask) == value)
			return true;
		if (now_ms() > deadline)
			break;
		poll();
		IOSleep(1);
	}
	LOG("controller not ready; %s gave up, CSTS=0x%x", what, csts);
	return false;
}

/*
 * apple_nvme_submit_cmd_t8103(): the command into its slot of the linear submission
 * queue, its TCB into the NVMMU's table, the tag into the doorbell. THE ONLY DOORBELL
 * WRITE OF THIS DRIVER, behind the gate.
 */
bool
SFAns::ring(unsigned queue, unsigned tag, const uint8_t sqeIn[64], bool hasData)
{
	uint8_t sqe[SQE_SIZE];
	uint8_t *sq = (queue == SF_NVME_Q_ADMIN ? asq.va : iosq.va) + tag * SQE_SIZE;
	uint8_t *tcb = (queue == SF_NVME_Q_ADMIN ? atcb.va : iotcb.va) + tag * TCB_SIZE;
	int reason;

	memcpy(sqe, sqeIn, SQE_SIZE);
	p16(sqe + SQE_CID, (uint16_t)tag);
	reason = (dead || !enabled || tag >= SF_ANS_IO_DEPTH) ? SF_NVME_GATE_QUEUE : sf_nvme_gate(queue, sqe, nsBlocks);
	if (reason == SF_NVME_GATE_OK) {
		memcpy(sq, sqe, SQE_SIZE);
		/* the bytes the controller will read */
		reason = sf_nvme_gate(queue, sq, nsBlocks);
		if (reason != SF_NVME_GATE_OK)
			bzero(sq, SQE_SIZE);
	}
	if (reason != SF_NVME_GATE_OK) {
		gateRejected++;
		gateLastReason = (uint32_t)reason;
		gateLastOpcode = sqe[0];
		gateLastQueue = queue;
		LOG("REFUSED: opcode 0x%02x on queue %u (reason %d); nothing was sent", sqe[0], queue, reason);
		return false;
	}

	uint64_t prp1 = g64(sqe + SQE_PRP1);
	tcb[TCB_OPCODE] = 0;
	p64(tcb + TCB_PRP1, prp1);
	p64(tcb + TCB_PRP2, g64(sqe + SQE_PRP2));
	p16(tcb + TCB_LENGTH, g16(sqe + SQE_CDW12));
	tcb[TCB_COMMAND_ID] = (uint8_t)tag;
	/*
	 * As Linux: no pointer, no DMA; an odd opcode reads host memory, an even one writes
	 * it. With the allowlist the odd ones are Create I/O CQ/SQ (the pointer is the queue
	 * itself); the I/O queue's only opcode is Read.
	 */
	if (prp1 == 0 || !hasData)
		tcb[TCB_DMA_FLAGS] = 0;
	else if (sqe[0] & 1)
		tcb[TCB_DMA_FLAGS] = ANS_TCB_DMA_TO_DEVICE;
	else
		tcb[TCB_DMA_FLAGS] = ANS_TCB_DMA_FROM_DEVICE;
	if (queue == SF_NVME_Q_IO && tcb[TCB_DMA_FLAGS] != ANS_TCB_DMA_FROM_DEVICE) {
		/* cannot happen: the gate admits Read only */
		bzero(sq, SQE_SIZE);
		gateRejected++;
		gateLastReason = SF_NVME_GATE_FIELDS;
		gateLastOpcode = sqe[0];
		gateLastQueue = queue;
		LOG("REFUSED: I/O command without device-to-host direction; nothing was sent");
		return false;
	}
	mem_sync();
	nvme.w32(queue == SF_NVME_Q_ADMIN ? ANS_LINEAR_ASQ_DB : ANS_LINEAR_IOSQ_DB, (uint32_t)tag);
	doorbells[queue]++;
	if (sqe[0] < 8)
		opcodeCount[queue][sqe[0]]++;
	return true;
}

/* One admin command on tag 0, waited for. 0, EIO with the status logged, or ETIMEDOUT. */
int
SFAns::admin(const uint8_t sqe[64], bool hasData, uint32_t timeoutMs)
{
	uint64_t start = sf_ans_now_us(), deadline = start + (uint64_t)timeoutMs * 1000;
	unsigned spins = 0;

	if (!ring(SF_NVME_Q_ADMIN, 0, sqe, hasData))
		return EPERM;
	adminCommands++;
	for (;;) {
		const uint8_t *cqe = acq.va + acqHead * CQE_SIZE;
		mem_sync();
		uint16_t status = g16(cqe + CQE_STATUS);
		if ((status & 1) == acqPhase) {
			uint16_t cid = g16(cqe + CQE_CID);

			nvme.w32(NVMMU_TCB_INVAL, cid);
			if (nvme.r32(NVMMU_TCB_STAT)) {
				tcbInvalFailures++;
				LOG("NVMMU TCB invalidation failed (admin tag %u)", cid);
			}
			if (++acqHead == SF_ANS_AQ_DEPTH) {
				acqHead = 0;
				acqPhase ^= 1;
			}
			nvme.w32(ANS_ACQ_DB, acqHead);
			if (cid != 0) {
				strayCompletions++;
				LOG("admin completion for tag %u, expected 0", cid);
				continue;
			}
			if (status >> 1) {
				cmdErrors++;
				LOG("admin opcode 0x%02x failed: status 0x%x", sqe[0], status >> 1);
				return EIO;
			}
			return 0;
		}
		if (sf_ans_now_us() > deadline || crashed) {
			cmdTimeouts++;
			dead = true;
			LOG("admin opcode 0x%02x: no completion in %u ms (CSTS=0x%x); the controller is not used any more", sqe[0],
			    timeoutMs, nvme.r32(NVME_REG_CSTS));
			return ETIMEDOUT;
		}
		poll();
		if (++spins < 200)
			IODelay(10);
		else
			IOSleep(1);
	}
}

int
SFAns::identify(uint32_t cns, uint32_t nsid, uint8_t *out)
{
	uint8_t sqe[SQE_SIZE] = { 0 };
	int ret;

	sqe[0] = SF_NVME_ADMIN_IDENTIFY;
	p32(sqe + SQE_NSID, nsid);
	p64(sqe + SQE_PRP1, adminData.phys);
	p32(sqe + SQE_CDW10, cns);
	bzero(adminData.va, 4096);
	ret = admin(sqe, true, ADMIN_TIMEOUT_MS);
	if (ret == 0)
		memcpy(out, adminData.va, 4096);
	return ret;
}

int
SFAns::readSmart(void)
{
	uint8_t sqe[SQE_SIZE] = { 0 };
	int ret;

	sqe[0] = SF_NVME_ADMIN_GET_LOG_PAGE;
	p32(sqe + SQE_NSID, 0xffffffffu);
	p64(sqe + SQE_PRP1, adminData.phys);
	p32(sqe + SQE_CDW10, SF_NVME_LOG_SMART | (127u << 16));
	bzero(adminData.va, 4096);
	ret = admin(sqe, true, ADMIN_TIMEOUT_MS);
	if (ret == 0)
		memcpy(smart, adminData.va, sizeof(smart));
	return ret;
}

void
SFAns::observe(void)
{
	uint32_t ans2 = sf_pmgr_state_named("ANS2"), st = sf_pmgr_state_named("APCIE_ST"), apcie = sf_pmgr_state_named("APCIE");

	LOG("observe: PMGR ANS2=0x%08x APCIE_ST=0x%08x APCIE=0x%08x", ans2, st, apcie);
	if (((ans2 >> 4) & 0xf) != 0xf) {
		LOG("observe: the ANS2 domain is not on; its registers are not read");
		return;
	}
	if (!asc.mapNode("/arm-io/ans", 0) || !sart.mapNode("/arm-io/sart-ans", 0)) {
		LOG("observe: /arm-io/ans or /arm-io/sart-ans could not be mapped");
		return;
	}
	LOG("observe: asc 0x%llx (0x%llx bytes): cpu_control=0x%08x a2i=0x%08x i2a=0x%08x", asc.phys, asc.len,
	    asc.r32(ANS_COPROC_CPU_CONTROL), asc.r32(ASC_MBOX + MBOX_A2I_CONTROL), asc.r32(ASC_MBOX + MBOX_I2A_CONTROL));
	for (unsigned i = 0; i < SF_ANS_SART_ENTRIES; i++) {
		uint32_t cfg = sart.r32(SART2_CONFIG(i)), paddr = sart.r32(SART2_PADDR(i));
		if (cfg != 0 || paddr != 0)
			LOG("observe: SART entry %u: config 0x%08x paddr 0x%08x", i, cfg, paddr);
	}
	if (((st >> 4) & 0xf) != 0xf) {
		LOG("observe: the APCIE_ST domain is not on; the NVMe registers are not read");
		return;
	}
	if (!nvme.mapNode("/arm-io/ans", 3)) {
		LOG("observe: the NVMe registers could not be mapped");
		return;
	}
	LOG("observe: nvme 0x%llx (0x%llx bytes): CAP=0x%016llx VS=0x%08x CC=0x%08x CSTS=0x%08x AQA=0x%08x boot_status=0x%08x "
	    "linear_sq=0x%08x nvmmu_num=0x%08x", nvme.phys, nvme.len, OSReadLittleInt64(nvme.va, NVME_REG_CAP),
	    nvme.r32(NVME_REG_VS), nvme.r32(NVME_REG_CC), nvme.r32(NVME_REG_CSTS), nvme.r32(NVME_REG_AQA),
	    nvme.r32(ANS_BOOT_STATUS), nvme.r32(ANS_LINEAR_SQ_CTRL), nvme.r32(NVMMU_NUM_TCBS));
}

int
SFAns::start(char *why, size_t whyLen)
{
	uint64_t t0 = now_ms();
	uint32_t control, lbaf, timeout;
	uint8_t sqe[SQE_SIZE];

#define FAIL(code, ...) do { snprintf(why, whyLen, __VA_ARGS__); LOG("%s", why); return (code); } while (0)
	/*
	 * Every start begins from the same state (the first of a boot and a later one after
	 * a clean stop, which is what Linux does on a driver rebind). The firmware's shared
	 * buffers of an earlier start are not reused: they are simply left allocated.
	 */
	bzero(endpoints, sizeof(endpoints));
	version = 0;
	helloSeen = epmapDone = bootFailed = crashed = iopAck = apAck = cpuStarted = false;
	iopPower = apPower = 0;
	for (unsigned i = 0; i < SF_ANS_SHMEM; i++)
		shmem[i] = SFAnsMem();
	nshmem = 0;
	syslogEntries = syslogMsgSize = 0;
	enabled = ioQueues = dead = false;
	nsBlocks = 0;
	inflight = 0;
	sartUsed = 0;
	for (unsigned s = 0; s < SF_ANS_SLOTS; s++)
		slot[s].busy = false;
	/* memory first: nothing is touched when it cannot be had */
	if (!asq.alloc(0x4000) || !acq.alloc(0x4000) || !atcb.alloc(SF_ANS_IO_DEPTH * TCB_SIZE) || !iosq.alloc(0x4000) ||
	    !iocq.alloc(0x4000) || !iotcb.alloc(SF_ANS_IO_DEPTH * TCB_SIZE) || !adminData.alloc(0x4000))
		FAIL(ENOMEM, "no memory for the queues");
	for (unsigned s = 0; s < SF_ANS_SLOTS; s++) {
		if (!slot[s].data.alloc(SF_ANS_SLOT_BYTES) || !slot[s].prp.alloc(0x4000))
			FAIL(ENOMEM, "no memory for read slot %u", s);
		/* the PRP list of the slot: its pages 1.. (page 0 is PRP1) */
		for (unsigned i = 1; i < SF_NVME_MAX_READ_BLOCKS; i++)
			p64(slot[s].prp.va + (i - 1) * 8, slot[s].data.phys + (uint64_t)i * 4096);
	}

	/* power-domains = <&ps_ans2>, <&ps_apcie_st>: the node's clock-gates name APCIE_ST, whose parents are ANS2 and APCIE */
	if (sf_pmgr_enable_node("/arm-io/ans", -1, 0, true) != 0)
		FAIL(ENXIO, "the power domains of /arm-io/ans could not be turned on");
	if (!asc.mapNode("/arm-io/ans", 0) || !nvme.mapNode("/arm-io/ans", 3) || !sart.mapNode("/arm-io/sart-ans", 0))
		FAIL(ENXIO, "the registers of /arm-io/ans or /arm-io/sart-ans could not be mapped");
	if (asc.len < ASC_MBOX + 0x1000 || nvme.len < NVMMU_TCB_STAT + 4 || sart.len < 0x80)
		FAIL(ENXIO, "/arm-io/ans has unexpected register sizes");
	if (!sf_adt_u32("/arm-io/sart-ans", "sart-version", 0, &sartVersion) || sartVersion != 2)
		FAIL(ENOTSUP, "sart-ans is version %u, not 2", sartVersion);

	control = asc.r32(ANS_COPROC_CPU_CONTROL);
	LOG("found: cpu_control=0x%08x a2i=0x%08x i2a=0x%08x CC=0x%08x CSTS=0x%08x boot_status=0x%08x", control,
	    asc.r32(ASC_MBOX + MBOX_A2I_CONTROL), asc.r32(ASC_MBOX + MBOX_I2A_CONTROL), nvme.r32(NVME_REG_CC),
	    nvme.r32(NVME_REG_CSTS), nvme.r32(ANS_BOOT_STATUS));
	if (control & ANS_COPROC_CPU_CONTROL_RUN)
		FAIL(EBUSY, "the ANS is already running (cpu_control 0x%08x): not started by this driver, left alone", control);
	sartScan();

	/* reset_control_assert(); apple_rtkit_reinit(); reset_control_deassert() */
	if (sf_pmgr_reset_named("ANS2", true) != 0)
		FAIL(EIO, "the ANS2 domain could not be reset");
	if (!(asc.r32(ASC_MBOX + MBOX_I2A_CONTROL) & MBOX_CONTROL_EMPTY))
		FAIL(EBUSY, "the mailbox holds messages before the coprocessor was started; left alone");
	asc.w32(ANS_COPROC_CPU_CONTROL, ANS_COPROC_CPU_CONTROL_RUN);
	cpuStarted = true;

	/* apple_rtkit_boot() */
	if (!waitFlag(&epmapDone, RTKIT_WAIT_MS))
		FAIL(ETIMEDOUT, "RTKit: endpoint discovery failed (hello %d)", helloSeen);
	if (!waitFlag(&iopAck, RTKIT_WAIT_MS))
		FAIL(ETIMEDOUT, "RTKit: no IOP power acknowledgment");
	if (!rtkitPower(true, PWR_STATE_ON))
		FAIL(ETIMEDOUT, "RTKit: AP power on was not acknowledged");
	LOG("RTKit: booted in %u ms: protocol %u, IOP power 0x%x, AP power 0x%x, endpoints 0x%08x", (unsigned)(now_ms() - t0),
	    version, iopPower, apPower, endpoints[0]);

	{
		uint64_t deadline = now_ms() + ANS_BOOT_TIMEOUT_MS;
		while (nvme.r32(ANS_BOOT_STATUS) != ANS_BOOT_STATUS_OK) {
			if (now_ms() > deadline)
				FAIL(ETIMEDOUT, "ANS did not initialize (boot status 0x%08x)", nvme.r32(ANS_BOOT_STATUS));
			poll();
			IOSleep(1);
		}
	}

	/* linear submission queues and the NVMMU, for the maximum admin and I/O queue depth */
	nvme.w32(ANS_LINEAR_SQ_CTRL, ANS_LINEAR_SQ_EN);
	nvme.w32(ANS_MAX_PEND_CMDS_CTRL, SF_ANS_IO_DEPTH | (SF_ANS_IO_DEPTH << 16));
	nvme.w32(NVMMU_NUM_TCBS, SF_ANS_IO_DEPTH - 1);

	cc = nvme.r32(NVME_REG_CC);
	if ((cc & NVME_CC_ENABLE) || (nvme.r32(NVME_REG_CSTS) & NVME_CSTS_RDY))
		FAIL(EBUSY, "the controller is enabled after the reset (CC 0x%08x, CSTS 0x%08x); left alone", cc, nvme.r32(NVME_REG_CSTS));

	/* the admin queue */
	nvme.w32(NVME_REG_AQA, (SF_ANS_AQ_DEPTH - 1) | ((SF_ANS_AQ_DEPTH - 1) << 16));
	OSWriteLittleInt64(nvme.va, NVME_REG_ASQ, asq.phys);
	OSWriteLittleInt64(nvme.va, NVME_REG_ACQ, acq.phys);
	OSWriteLittleInt64(nvme.va, NVMMU_ASQ_TCB_BASE, atcb.phys);
	OSWriteLittleInt64(nvme.va, NVMMU_IOSQ_TCB_BASE, iotcb.phys);
	OSSynchronizeIO();

	/* nvme_enable_ctrl() */
	cap = OSReadLittleInt64(nvme.va, NVME_REG_CAP);
	vs = nvme.r32(NVME_REG_VS);
	if (NVME_CAP_MPSMIN(cap) + 12 > NVME_CTRL_PAGE_SHIFT)
		FAIL(ENOTSUP, "the controller's minimum page size is 2^%u", NVME_CAP_MPSMIN(cap) + 12);
	cc = (NVME_CAP_CSS(cap) & NVME_CAP_CSS_CSI) ? NVME_CC_CSS_CSI : NVME_CC_CSS_NVM;
	cc &= ~NVME_CC_CRIME;
	cc |= (NVME_CTRL_PAGE_SHIFT - 12) << NVME_CC_MPS_SHIFT;
	cc |= NVME_CC_AMS_RR | NVME_CC_SHN_NONE;
	cc |= NVME_CC_IOSQES | NVME_CC_IOCQES;
	nvme.w32(NVME_REG_CC, cc);
	cap = OSReadLittleInt64(nvme.va, NVME_REG_CAP);
	timeout = NVME_CAP_TIMEOUT(cap);
	if (cap & NVME_CAP_CRMS_CRWMS) {
		uint32_t crto = nvme.r32(NVME_REG_CRTO) & 0xffff;
		if (crto >= timeout)
			timeout = crto;
	}
	cc |= NVME_CC_ENABLE;
	nvme.w32(NVME_REG_CC, cc);
	enabled = true;
	acqHead = 0;
	acqPhase = 1;
	if (!waitCsts(NVME_CSTS_RDY, NVME_CSTS_RDY, ((timeout + 1) / 2) * 1000, "initialisation"))
		FAIL(ETIMEDOUT, "the controller did not become ready");
	LOG("controller enabled: CAP=0x%016llx VS=0x%08x CC=0x%08x CSTS=0x%08x (%u ms since start)", cap, vs, cc,
	    nvme.r32(NVME_REG_CSTS), (unsigned)(now_ms() - t0));

	if (identify(1, 0, idCtrl) != 0)
		FAIL(EIO, "Identify Controller failed");
	if (identify(0, SF_NVME_NSID, idNs) != 0)
		FAIL(EIO, "Identify Namespace 1 failed");
	lbaf = g32(idNs + 128 + 4 * (idNs[26] & 0xf));
	blockSize = 1u << ((lbaf >> 16) & 0xff);
	if (blockSize != 4096 || (lbaf & 0xffff) != 0)
		FAIL(ENOTSUP, "namespace 1 has %u-byte blocks with %u bytes of metadata; only 4096/0 is handled", blockSize,
		    lbaf & 0xffff);

	/* apple_nvme_create_cq(), apple_nvme_create_sq() */
	bzero(sqe, sizeof(sqe));
	sqe[0] = SF_NVME_ADMIN_CREATE_CQ;
	p64(sqe + SQE_PRP1, iocq.phys);
	p32(sqe + SQE_CDW10, SF_NVME_IO_QID | ((SF_ANS_IO_DEPTH - 1) << 16));
	p32(sqe + SQE_CDW11, NVME_QUEUE_PHYS_CONTIG | NVME_CQ_IRQ_ENABLED);
	if (admin(sqe, true, ADMIN_TIMEOUT_MS) != 0)
		FAIL(EIO, "Create I/O Completion Queue failed");
	bzero(sqe, sizeof(sqe));
	sqe[0] = SF_NVME_ADMIN_CREATE_SQ;
	p64(sqe + SQE_PRP1, iosq.phys);
	p32(sqe + SQE_CDW10, SF_NVME_IO_QID | ((SF_ANS_IO_DEPTH - 1) << 16));
	p32(sqe + SQE_CDW11, NVME_QUEUE_PHYS_CONTIG | (SF_NVME_IO_QID << 16));
	iocqHead = 0;
	iocqPhase = 1;
	ioQueues = true;        /* from here the completion queue exists and must be deleted */
	if (admin(sqe, true, ADMIN_TIMEOUT_MS) != 0)
		FAIL(EIO, "Create I/O Submission Queue failed");

	if (readSmart() != 0)
		FAIL(EIO, "the SMART log could not be read");
	/* reads are admitted only now */
	nsBlocks = g64(idNs + 0);
	LOG("ready in %u ms: namespace 1 has %llu blocks of %u bytes", (unsigned)(now_ms() - t0), nsBlocks, blockSize);
	return 0;
#undef FAIL
}

int
SFAns::freeSlot(void) const
{
	if (dead || !ioQueues || nsBlocks == 0)
		return -1;
	for (unsigned s = 0; s < SF_ANS_SLOTS; s++)
		if (!slot[s].busy)
			return (int)s;
	return -1;
}

int
SFAns::submitRead(unsigned s, uint64_t lba, uint32_t blocks, void *cookie)
{
	uint8_t sqe[SQE_SIZE] = { 0 };

	if (s >= SF_ANS_SLOTS || slot[s].busy || blocks == 0 || blocks > SF_NVME_MAX_READ_BLOCKS)
		return EINVAL;
	sqe[0] = SF_NVME_IO_READ;
	p32(sqe + SQE_NSID, SF_NVME_NSID);
	p64(sqe + SQE_PRP1, slot[s].data.phys);
	if (blocks == 2)
		p64(sqe + SQE_PRP2, slot[s].data.phys + 4096);
	else if (blocks > 2)
		p64(sqe + SQE_PRP2, slot[s].prp.phys);
	p32(sqe + SQE_CDW10, (uint32_t)lba);
	p32(sqe + SQE_CDW11, (uint32_t)(lba >> 32));
	p32(sqe + SQE_CDW12, blocks - 1);
	slot[s].busy = true;
	slot[s].cookie = cookie;
	slot[s].blocks = blocks;
	slot[s].submitted = sf_ans_now_us();
	if (!ring(SF_NVME_Q_IO, SF_ANS_AQ_DEPTH + s, sqe, true)) {
		slot[s].busy = false;
		return EPERM;
	}
	inflight++;
	readCommands++;
	readBlocks += blocks;
	return 0;
}

/* apple_nvme_poll_cq() for the I/O queue */
unsigned
SFAns::reap(SFAnsCompletion *out, unsigned max)
{
	unsigned n = 0;
	bool moved = false;

	poll();
	if (!ioQueues)
		return 0;
	while (n < max) {
		const uint8_t *cqe = iocq.va + iocqHead * CQE_SIZE;
		mem_sync();
		uint16_t status = g16(cqe + CQE_STATUS);
		if ((status & 1) != iocqPhase)
			break;
		uint16_t cid = g16(cqe + CQE_CID);

		nvme.w32(NVMMU_TCB_INVAL, cid);
		if (nvme.r32(NVMMU_TCB_STAT)) {
			tcbInvalFailures++;
			LOG("NVMMU TCB invalidation failed (tag %u)", cid);
		}
		if (++iocqHead == SF_ANS_IO_DEPTH) {
			iocqHead = 0;
			iocqPhase ^= 1;
		}
		moved = true;
		if (cid < SF_ANS_AQ_DEPTH || cid >= SF_ANS_AQ_DEPTH + SF_ANS_SLOTS || !slot[cid - SF_ANS_AQ_DEPTH].busy) {
			strayCompletions++;
			LOG("completion for tag %u, which is not in flight", cid);
			continue;
		}
		SFAnsSlot *sl = &slot[cid - SF_ANS_AQ_DEPTH];
		out[n].slot = cid - SF_ANS_AQ_DEPTH;
		out[n].status = status >> 1;
		out[n].cookie = sl->cookie;
		out[n].micros = sf_ans_now_us() - sl->submitted;
		if (status >> 1)
			cmdErrors++;
		sl->busy = false;
		inflight--;
		n++;
	}
	if (moved)
		nvme.w32(ANS_IOCQ_DB, iocqHead);
	return n;
}

unsigned
SFAns::overdue(uint64_t micros)
{
	uint64_t now = sf_ans_now_us();
	unsigned n = 0;

	for (unsigned s = 0; s < SF_ANS_SLOTS; s++)
		if (slot[s].busy && now - slot[s].submitted > micros)
			n++;
	if (n != 0 && !dead) {
		dead = true;
		cmdTimeouts += n;
		LOG("%u read(s) without completion after %llu ms (CSTS=0x%x); the controller is not used any more", n,
		    micros / 1000, nvme.r32(NVME_REG_CSTS));
	}
	return n;
}

bool
SFAns::stop(char *why, size_t whyLen)
{
	bool ok = true;
	uint32_t csts = 0;
	uint8_t sqe[SQE_SIZE];

	snprintf(why, whyLen, "clean");
	if (nvme.va != NULL && enabled) {
		csts = nvme.r32(NVME_REG_CSTS);
		/* apple_nvme_disable_legacy(shutdown = true) */
		if (crashed || !(csts & NVME_CSTS_RDY) || (csts & NVME_CSTS_CFS))
			dead = true;
		if (!dead) {
			if (ioQueues) {
				bzero(sqe, sizeof(sqe));
				sqe[0] = SF_NVME_ADMIN_DELETE_SQ;
				p32(sqe + SQE_CDW10, SF_NVME_IO_QID);
				if (admin(sqe, false, ADMIN_TIMEOUT_MS) != 0)
					ok = false;
				bzero(sqe, sizeof(sqe));
				sqe[0] = SF_NVME_ADMIN_DELETE_CQ;
				p32(sqe + SQE_CDW10, SF_NVME_IO_QID);
				if (!dead && admin(sqe, false, ADMIN_TIMEOUT_MS) != 0)
					ok = false;
				ioQueues = false;
			}
			/* nvme_disable_ctrl(shutdown = true): a normal shutdown notification, waited for */
			uint32_t rtd3e = g32(idCtrl + 88) / 1000000u, seconds = rtd3e < 5 ? 5 : (rtd3e > 60 ? 60 : rtd3e);
			cc = (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL;
			nvme.w32(NVME_REG_CC, cc);
			if (!waitCsts(NVME_CSTS_SHST_MASK, NVME_CSTS_SHST_CMPLT, seconds * 1000, "shutdown")) {
				ok = false;
				snprintf(why, whyLen, "the shutdown notification was not completed (CSTS 0x%x)", nvme.r32(NVME_REG_CSTS));
			}
			/* nvme_disable_ctrl(shutdown = false) */
			cc &= ~NVME_CC_SHN_MASK;
			cc &= ~NVME_CC_ENABLE;
			nvme.w32(NVME_REG_CC, cc);
			if (!waitCsts(NVME_CSTS_RDY, 0, ((NVME_CAP_TIMEOUT(cap) + 1) / 2) * 1000, "reset")) {
				ok = false;
				snprintf(why, whyLen, "the controller did not leave the ready state (CSTS 0x%x)", nvme.r32(NVME_REG_CSTS));
			}
			LOG("controller shut down and disabled: CC=0x%08x CSTS=0x%08x", nvme.r32(NVME_REG_CC), nvme.r32(NVME_REG_CSTS));
		} else {
			ok = false;
			snprintf(why, whyLen, "the controller was dead (CSTS 0x%x, firmware crashed %d): no shutdown notification", csts,
			    crashed);
			LOG("%s", why);
		}
		enabled = false;
		ioQueues = false;
	}
	nsBlocks = 0;
	/* apple_rtkit_shutdown(), then the coprocessor is stopped */
	if (asc.va != NULL && cpuStarted) {
		if (rtkitRunning()) {
			bool quiesced = rtkitPower(true, PWR_STATE_QUIESCED);
			bool asleep = quiesced && rtkitPower(false, PWR_STATE_SLEEP);
			if (!asleep) {
				ok = false;
				snprintf(why, whyLen, "RTKit did not acknowledge the shutdown (AP 0x%x, IOP 0x%x)", apPower, iopPower);
			}
			asc.w32(ANS_COPROC_CPU_CONTROL, 0);
			LOG("RTKit shut down (AP 0x%x, IOP 0x%x); coprocessor stopped: cpu_control=0x%08x", apPower, iopPower,
			    asc.r32(ANS_COPROC_CPU_CONTROL));
		} else {
			/* as Linux: a coprocessor whose RTKit never came up is not sent a shutdown */
			ok = false;
			snprintf(why, whyLen, "RTKit was not running (AP 0x%x, IOP 0x%x, crashed %d); coprocessor left as it is", apPower,
			    iopPower, crashed);
			LOG("%s", why);
		}
		cpuStarted = false;
		/* only once the firmware no longer runs: apple_sart_shutdown() */
		if (asc.r32(ANS_COPROC_CPU_CONTROL) == 0)
			sartClear();
	}
	return ok;
}
