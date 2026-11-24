// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * The always-on coprocessor of T8103 as a polled client. See sf_aop.h.
 *
 * Ported from the Omacom Linux tree (omacom/linux-aurora, integration/aurora):
 *   drivers/soc/apple/aop.rs       "Apple AOP driver", Copyright (C) The Asahi Linux
 *                                  Contributors (GPL-2.0-only OR MIT): boot arguments,
 *                                  CPU start, AFK endpoint handshake, EPIC headers, calls
 *                                  and replies, service announcements
 *   drivers/soc/apple/aop_ring.rs  bounds of the AFK ring entries (same licence)
 *   drivers/soc/apple/rtkit.c      "Apple RTKit IPC library", Copyright (C) The Asahi Linux
 *                                  Contributors (GPL-2.0-only OR MIT): management, syslog,
 *                                  ioreport, oslog and crashlog endpoints, power states
 *   drivers/soc/apple/mailbox.c    ASC mailbox registers (GPL-2.0-only OR MIT)
 * The address filter of dart-aop is programmed as m1n1 does before it starts Linux
 * (src/dapf.c dapf_init_t8020, MIT, Copyright The Asahi Linux Contributors).
 *
 * Differences from Linux, all of them in how the host waits, none in what is sent: the
 * mailbox is polled from one thread instead of served by interrupts and work queues, so
 * a call waits by polling; every shared buffer comes from one fixed DART window on
 * stream 0 instead of the DMA allocator, and is never freed.
 */
#include "sf_aop.h"

#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <kern/clock.h>
#include <libkern/OSByteOrder.h>
#include <string.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFAop: " fmt "\n", ##__VA_ARGS__)
#define VLOG(fmt, ...) do { if (verbose) IOLog("SFAop: " fmt "\n", ##__VA_ARGS__); } while (0)

/* ---- aop.rs ---- */
#define BOOTARGS_OFFSET      0x22c
#define BOOTARGS_SIZE        0x230
#define AFK_OPC_GET_BUF      0x89
#define AFK_OPC_INIT         0x80
#define AFK_OPC_INIT_RX      0x8b
#define AFK_OPC_INIT_TX      0x8a
#define AFK_OPC_INIT_UNK     0x8c
#define AFK_OPC_SEND         0xa2ULL
#define AFK_OPC_START_ACK    0x86
#define AFK_OPC_SHUTDOWN_ACK 0xc1
#define AFK_OPC_RECV         0x85
#define AFK_MSG_GET_BUF_ACK  (0xa1ULL << 48)
#define AFK_MSG_INIT         (0x80ULL << 48)
#define AFK_MSG_INIT_ACK     (0xa0ULL << 48)
#define AFK_MSG_START        (0xa3ULL << 48)
#define AFK_RB_BLOCK_STEP    0x40
#define EPIC_TYPE_NOTIFY     0
#define EPIC_CATEGORY_REPORT 0x00
#define EPIC_CATEGORY_NOTIFY 0x10
#define EPIC_CATEGORY_REPLY  0x20
#define EPIC_SUBTYPE_STD_SERVICE     0xc0
#define EPIC_SUBTYPE_FAKEHID_REPORT  0xc4
#define EPIC_SUBTYPE_RETCODE         0x84
#define EPIC_SUBTYPE_RETCODE_PAYLOAD 0xa0
#define EPIC_SUBTYPE_STRING          0x8a
#define QE_MAGIC1            0x20504f49u    /* from_fourcc(" POI") */
#define QE_MAGIC2            0x20504f41u    /* from_fourcc(" POA") */
#define QEH_SIZE             16
#define EPIC_HEADER_SIZE     40
#define EPIC_ANNOUNCE_CHANNEL_OFFSET 32
#define EPIC_ANNOUNCE_LEN    44
#define EPIC_CALL_TIMEOUT_MS 5000

/* HW_CFG_T8103 */
#define T8103_EC0P           0x020000ULL
#define T8103_AOPT           1ULL
#define T8103_ALIG           128ULL
#define FOURCC(a, b, c, d)   (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* ---- rtkit.rs, mailbox.c ---- */
#define ASC_CPU_CONTROL      0x44
#define ASC_CPU_CONTROL_RUN  (1u << 4)
#define ASC_CPU_STATUS       0x48
#define ASC_MBOX             0x8000         /* the mailbox node of the Linux device tree: +0x8000 */
#define MBOX_CONTROL_FULL    (1u << 16)
#define MBOX_CONTROL_EMPTY   (1u << 17)
#define MBOX_A2I_CONTROL     0x110
#define MBOX_A2I_SEND0       0x800
#define MBOX_A2I_SEND1       0x808
#define MBOX_I2A_CONTROL     0x114
#define MBOX_I2A_RECV0       0x830
#define MBOX_I2A_RECV1       0x838
#define MBOX_TX_TIMEOUT_MS   500

/* ---- rtkit.c ---- */
#define PWR_STATE_ON         0x20
#define PWR_STATE_INIT       0x220
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
#define RTKIT_WAIT_MS        1000           /* apple_rtkit_wait_for_completion() */

#define PAGE_16K             0x4000u

static inline uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t le32(const uint8_t *p) { return OSReadLittleInt32(p, 0); }
static inline void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void put32(uint8_t *p, uint32_t v) { OSWriteLittleInt32(p, 0, v); }
static inline void mem_sync(void) { __asm__ volatile("dsb sy" ::: "memory"); }

/* A little-endian u32 at any alignment of a device mapping, where an unaligned access faults. */
static inline uint32_t
dev_le32(volatile const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
now_ms(void)
{
	uint64_t abs, ns;
	clock_get_uptime(&abs);
	absolutetime_to_nanoseconds(abs, &ns);
	return ns / 1000000ULL;
}

/* ---- mailbox ------------------------------------------------------------------------ */

bool
SFAop::mboxRecv(uint64_t *msg, uint8_t *epOut)
{
	volatile uint8_t *mb = asc.va + ASC_MBOX;

	if (OSReadLittleInt32(mb, MBOX_I2A_CONTROL) & MBOX_CONTROL_EMPTY)
		return false;
	uint64_t m0 = OSReadLittleInt64(mb, MBOX_I2A_RECV0);
	uint64_t m1 = OSReadLittleInt64(mb, MBOX_I2A_RECV1);
	OSSynchronizeIO();
	/* reads of buffers named by the message come after the message */
	mem_sync();
	*msg = m0;
	*epOut = (uint8_t)m1;
	rxMessages++;
	return true;
}

bool
SFAop::mboxSend(uint8_t epNo, uint64_t msg)
{
	volatile uint8_t *mb = asc.va + ASC_MBOX;
	uint64_t deadline = now_ms() + MBOX_TX_TIMEOUT_MS;

	if (crashed) {
		LOG("the firmware has crashed; 0x%016llx to endpoint 0x%02x not sent", msg, epNo);
		return false;
	}
	if (epNo >= APP_ENDPOINT_START && !running()) {
		LOG("endpoint 0x%02x is not running; 0x%016llx not sent", epNo, msg);
		return false;
	}
	while (OSReadLittleInt32(mb, MBOX_A2I_CONTROL) & MBOX_CONTROL_FULL) {
		if (now_ms() > deadline) {
			LOG("mailbox full for %u ms; 0x%016llx to endpoint 0x%02x not sent", MBOX_TX_TIMEOUT_MS, msg, epNo);
			return false;
		}
		IOSleep(1);
	}
	/* writes to buffers named by the message are visible before the message */
	mem_sync();
	VLOG("> ep 0x%02x 0x%016llx", epNo, msg);
	OSWriteLittleInt64(mb, MBOX_A2I_SEND0, msg);
	OSWriteLittleInt64(mb, MBOX_A2I_SEND1, (uint64_t)epNo);
	OSSynchronizeIO();
	txMessages++;
	return true;
}

bool
SFAop::mgmtSend(uint8_t type, uint64_t msg)
{
	msg &= ~(0xffULL << 52);
	msg |= (uint64_t)type << 52;
	return mboxSend(EP_MGMT, msg);
}

/* apple_rtkit_start_ep() */
bool
SFAop::startEp(uint8_t epNo)
{
	if (!hasEndpoint(epNo))
		return false;
	if (epNo >= APP_ENDPOINT_START && !running())
		return false;
	return mgmtSend(MGMT_STARTEP, ((uint64_t)epNo << 32) | MGMT_STARTEP_FLAG);
}

bool
SFAop::waitFlag(const bool *flag, uint32_t ms)
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
SFAop::pollFor(uint32_t ms)
{
	uint64_t deadline = now_ms() + ms;

	do {
		poll();
		IOSleep(1);
	} while (now_ms() < deadline);
}

/* 16 KiB granules of the window, zeroed (the window was zeroed when it was made and nothing is reused). */
void *
SFAop::windowAlloc(uint32_t bytes, uint64_t *iova)
{
	uint32_t need = (bytes + PAGE_16K - 1) & ~(PAGE_16K - 1);

	if (dart.buf == NULL || need == 0 || need < bytes || need > dart.size - dartUsed)
		return NULL;
	void *p = dart.buf + dartUsed;
	*iova = dart.iova + dartUsed;
	dartUsed += need;
	return p;
}

/* ---- RTKit system endpoints (rtkit.c) ----------------------------------------------- */

void
SFAop::rxMgmt(uint64_t msg)
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
		LOG("RTKit: HELLO, versions %u..%u; initializing (protocol version %u)", minVer, maxVer, want);
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
				/* without starting these RTKit refuses to boot */
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
		LOG("RTKit: system endpoints started (map 0x%08x)", endpoints[0]);
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
		LOG("RTKit: unknown management message 0x%016llx (type 0x%02x)", msg, MGMT_TYPE(msg));
		unknownMessages++;
	}
}

/* apple_rtkit_common_rx_get_buffer() */
void
SFAop::rxBuffer(uint8_t epNo, uint64_t msg, uint8_t **buf, uint32_t *size, uint64_t *iova, const char *what)
{
	uint64_t wantSize, wantIova, reply;

	if (epNo == EP_OSLOG) {
		wantSize = (msg >> 36) & 0xfffff;
		wantIova = (msg & 0xfffffffffULL) << 12;
	} else {
		wantSize = ((msg >> 44) & 0xff) << 12;
		wantIova = msg & 0xfffffffffffULL;
	}
	if (wantSize == 0) {
		LOG("RTKit: %s buffer request of no size (0x%016llx); not answered", what, msg);
		return;
	}
	if (*size != 0) {
		/* a repeated request: answer it again when identical, refuse a changed one */
		if (wantSize != *size || (wantIova != *iova && wantIova != 0)) {
			LOG("RTKit: %s buffer request for 0x%llx bytes at 0x%llx while 0x%x bytes at 0x%llx are live; "
			    "not answered", what, wantSize, wantIova, *size, *iova);
			return;
		}
	} else {
		if (wantIova != 0) {
			LOG("RTKit: %s buffer request names its own address 0x%llx; not answered", what, wantIova);
			return;
		}
		uint64_t at = 0;
		void *p = windowAlloc((uint32_t)wantSize, &at);
		if (p == NULL) {
			LOG("RTKit: no room for the %s buffer of 0x%llx bytes (0x%x of 0x%x used); not answered", what,
			    wantSize, dartUsed, dart.size);
			return;
		}
		*buf = (uint8_t *)p;
		*size = (uint32_t)wantSize;
		*iova = at;
		LOG("RTKit: %s buffer: 0x%x bytes at iova 0x%llx", what, *size, *iova);
	}
	if (epNo == EP_OSLOG)
		reply = ((uint64_t)OSLOG_BUFFER_REQUEST << 56) | ((uint64_t)*size << 36) | (*iova >> 12);
	else
		reply = ((uint64_t)BUFFER_REQUEST << 52) | ((uint64_t)(*size >> 12) << 44) | *iova;
	mboxSend(epNo, reply);
}

void
SFAop::rxSyslog(uint64_t msg)
{
	switch (MGMT_TYPE(msg)) {
	case BUFFER_REQUEST:
		rxBuffer(EP_SYSLOG, msg, &syslogBuf, &syslogSize, &syslogIova, "syslog");
		break;
	case SYSLOG_INIT:
		syslogEntries = (uint32_t)(msg & 0xff);
		syslogMsgSize = (uint32_t)((msg >> 24) & 0xff);
		LOG("RTKit: syslog: %u entries of %u bytes", syslogEntries, syslogMsgSize);
		break;
	case SYSLOG_LOG: {
		uint32_t idx = (uint32_t)(msg & 0xff), entry = 0x20 + syslogMsgSize;
		char context[24], text[256];

		if (syslogBuf != NULL && syslogMsgSize != 0 && idx < syslogEntries && (idx + 1) * entry <= syslogSize) {
			uint32_t n = syslogMsgSize < sizeof(text) - 1 ? syslogMsgSize : (uint32_t)sizeof(text) - 1;
			memcpy(context, syslogBuf + idx * entry + 8, sizeof(context));
			context[sizeof(context) - 1] = 0;
			memcpy(text, syslogBuf + idx * entry + 8 + sizeof(context), n);
			text[n] = 0;
			n = (uint32_t)strlen(text);
			while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' '))
				text[--n] = 0;
			for (uint32_t i = 0; i < n; i++)
				if (text[i] < 0x20 || text[i] > 0x7e)
					text[i] = '.';
			syslogLines++;
			LOG("syslog: %s: %s", context, text);
		} else {
			LOG("RTKit: syslog record %u cannot be read (0x%016llx)", idx, msg);
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
SFAop::rxIoreport(uint64_t msg)
{
	switch (MGMT_TYPE(msg)) {
	case BUFFER_REQUEST:
		rxBuffer(EP_IOREPORT, msg, &ioreportBuf, &ioreportSize, &ioreportIova, "ioreport");
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
SFAop::rxOslog(uint64_t msg)
{
	unsigned type = (unsigned)(msg >> 56);

	switch (type) {
	case OSLOG_BUFFER_REQUEST:
		rxBuffer(EP_OSLOG, msg, &oslogBuf, &oslogSize, &oslogIova, "oslog");
		break;
	case OSLOG_LOG:
		if (oslogSize == 0 || (msg & 0x00ffffff00000000ULL)) {
			LOG("RTKit: invalid oslog notification 0x%016llx", msg);
			break;
		}
		mboxSend(EP_OSLOG, msg);
		break;
	case 3: case 4: case 5:
		/* notifications that expect no reply */
		break;
	default:
		LOG("RTKit: unknown oslog message 0x%016llx", msg);
		unknownMessages++;
	}
}

void
SFAop::rxCrashlog(uint64_t msg)
{
	if (MGMT_TYPE(msg) == BUFFER_REQUEST && crashlogSize == 0) {
		rxBuffer(EP_CRASHLOG, msg, &crashlogBuf, &crashlogSize, &crashlogIova, "crashlog");
		return;
	}
	if (MGMT_TYPE(msg) != BUFFER_REQUEST) {
		LOG("RTKit: unknown crashlog message 0x%016llx", msg);
		unknownMessages++;
		return;
	}
	/* the same message once the buffer exists: the firmware has crashed */
	crashed = true;
	LOG("RTKit: THE AOP FIRMWARE CRASHED (0x%016llx)", msg);
	if (crashlogBuf != NULL && crashlogSize >= 0x40) {
		const uint8_t *b = crashlogBuf;
		LOG("crashlog: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x "
		    "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4],
		    b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15], b[16], b[17], b[18], b[19],
		    b[20], b[21], b[22], b[23], b[24], b[25], b[26], b[27], b[28], b[29], b[30], b[31]);
	}
}

unsigned
SFAop::poll(void)
{
	unsigned n = 0;
	uint64_t msg;
	uint8_t epNo;

	if (asc.va == NULL)
		return 0;
	while (n < 256 && mboxRecv(&msg, &epNo)) {
		n++;
		VLOG("< ep 0x%02x 0x%016llx", epNo, msg);
		if (!hasEndpoint(epNo) && epNo != EP_MGMT)
			LOG("RTKit: message 0x%016llx on undiscovered endpoint 0x%02x", msg, epNo);
		switch (epNo) {
		case EP_MGMT:     rxMgmt(msg); break;
		case EP_CRASHLOG: rxCrashlog(msg); break;
		case EP_SYSLOG:   rxSyslog(msg); break;
		case EP_IOREPORT: rxIoreport(msg); break;
		case EP_OSLOG:    rxOslog(msg); break;
		default:
			if (epNo >= SF_AOP_EP_FIRST && epNo < SF_AOP_EP_FIRST + SF_AOP_EP_COUNT) {
				rxAfk(&ep[epNo - SF_AOP_EP_FIRST], msg);
			} else {
				LOG("RTKit: message 0x%016llx on unexpected endpoint 0x%02x", msg, epNo);
				unknownMessages++;
			}
		}
	}
	return n;
}

/* ---- AFK rings (aop_ring.rs) --------------------------------------------------------- */

static bool
ring_check_ptr(const SFAopRing *r, uint32_t ptr)
{
	return ptr < r->size && ptr % r->block == 0 && r->size - ptr >= QEH_SIZE;
}

/* Where the entry of `len` payload bytes at `ptr` ends, rounded to a block, wrapped. */
static bool
ring_end(const SFAopRing *r, uint32_t ptr, uint32_t len, uint32_t *out)
{
	if (!ring_check_ptr(r, ptr))
		return false;
	uint64_t end = (uint64_t)ptr + QEH_SIZE + len;
	uint64_t aligned = (end + r->block - 1) & ~((uint64_t)r->block - 1);
	if (aligned > r->size)
		return false;
	*out = (uint32_t)(aligned % r->size);
	return true;
}

static bool
magic_ok(const uint8_t *qeh)
{
	uint32_t magic = le32(qeh);
	return magic == QE_MAGIC1 || magic == QE_MAGIC2;
}

/* AFKEndpoint::parse_ring_buf() */
bool
SFAop::afkParseRing(SFAopEndpoint *e, uint64_t msg, SFAopRing *out)
{
	uint32_t size = (uint32_t)((msg >> 16) & 0xffff) * AFK_RB_BLOCK_STEP;
	uint32_t offset = (uint32_t)((msg >> 32) & 0xffff) * AFK_RB_BLOCK_STEP;

	if (e->buf == NULL || offset + 4 > e->bufSize)
		return false;
	uint32_t bufSize = le32(e->buf + offset);
	if (bufSize == 0 || bufSize > size)
		return false;
	uint32_t block = (size - bufSize) / 3;
	if ((uint64_t)offset + size > e->bufSize || block == 0 || (block & (block - 1)) != 0 ||
	    bufSize < QEH_SIZE || bufSize % block != 0)
		return false;
	out->off = offset;
	out->block = block;
	out->size = bufSize;
	out->valid = true;
	return true;
}

void
SFAop::afkDrop(SFAopEndpoint *e, const char *why, uint32_t a, uint32_t b, uint32_t c)
{
	e->dropped++;
	if ((e->dropped & (e->dropped - 1)) == 0)
		LOG("endpoint 0x%02x dropped a message: %s (0x%x 0x%x %u; %u so far)", e->index, why, a, b, c, e->dropped);
}

/* AFKEndpoint::handle_ipc(): 0 or the errno value Linux drops the message with */
int
SFAop::afkHandle(SFAopEndpoint *e, uint32_t channel, const uint8_t *msg, uint32_t len)
{
	const uint8_t *data = msg + EPIC_HEADER_SIZE;
	uint32_t dataLen = len - EPIC_HEADER_SIZE;
	uint8_t category = msg[21];
	uint16_t subtype = le16(msg + 22);
	uint16_t tag = le16(msg + (msg[20] >= 4 ? 32 : 24));

	if (category == EPIC_CATEGORY_REPORT) {
		e->reports++;
		if (subtype == EPIC_SUBTYPE_STD_SERVICE) {
			if (dataLen < EPIC_ANNOUNCE_LEN)
				return EMSGSIZE;
			e->announces++;
			if (nservices < SF_AOP_MAX_SERVICES) {
				SFAopService *s = &services[nservices];
				memcpy(s->name, data, SF_AOP_NAME_LEN);
				s->name[SF_AOP_NAME_LEN] = 0;
				s->ep = e->index;
				s->channel = le32(data + EPIC_ANNOUNCE_CHANNEL_OFFSET);
				nservices++;
				LOG("EPIC: endpoint 0x%02x announces service \"%s\" on channel %u (entry channel %u, "
				    "tail 0x%08x 0x%08x)", s->ep, s->name, s->channel, channel, le32(data + 36), le32(data + 40));
			}
			return 0;
		}
		if (subtype == EPIC_SUBTYPE_FAKEHID_REPORT)
			return 0;       /* nobody listens here; Linux hands it to no listener as well */
		return ENOENT;          /* a report nobody listens for */
	}
	if (category == EPIC_CATEGORY_REPLY) {
		if (subtype != EPIC_SUBTYPE_RETCODE_PAYLOAD && subtype != EPIC_SUBTYPE_RETCODE && subtype != EPIC_SUBTYPE_STRING)
			return EINVAL;
		if (dataLen < 4)
			return EMSGSIZE;
		if (tag == 0 || tag > SF_AOP_MAX_CALLS)
			return EINVAL;
		unsigned slot = tag - 1u;
		e->replies++;
		if (e->call[slot] == 1) {
			e->retcode[slot] = le32(data);
			e->call[slot] = 2;
			return 0;
		}
		if (e->call[slot] == 3) {
			LOG("EPIC: late reply for tag %u on endpoint 0x%02x", tag, e->index);
			e->call[slot] = 0;
			return 0;
		}
		return ENOENT;
	}
	return EINVAL;
}

/* AFKEndpoint::recv_rb() */
void
SFAop::afkRecvRing(SFAopEndpoint *e)
{
	const SFAopRing *r = &e->rx;

	if (!r->valid) {
		LOG("endpoint 0x%02x: Recv without a receive ring", e->index);
		return;
	}
	uint8_t *base = e->buf + r->off + 3 * r->block;
	uint32_t rptr = le32(e->buf + r->off + r->block);
	uint32_t wptr = le32(e->buf + r->off + 2 * r->block);
	uint32_t budget = r->size / r->block;

	mem_sync();
	for (;;) {
		if (!ring_check_ptr(r, rptr) || !ring_check_ptr(r, wptr)) {
			LOG("endpoint 0x%02x: ring pointers out of range: rptr 0x%x wptr 0x%x", e->index, rptr, wptr);
			return;
		}
		if (wptr == rptr || budget == 0)
			break;
		budget--;

		/* Ring::read_entry() */
		uint32_t ptr = rptr, next = 0;
		uint8_t qeh[QEH_SIZE];
		memcpy(qeh, base + ptr, QEH_SIZE);
		bool ok = magic_ok(qeh);
		if (ok && le32(qeh + 4) > r->size - ptr - QEH_SIZE) {
			ptr = 0;
			memcpy(qeh, base + ptr, QEH_SIZE);
			ok = magic_ok(qeh);
		}
		uint32_t size = le32(qeh + 4), channel = le32(qeh + 8);
		ok = ok && ring_end(r, ptr, size, &next) && next != ptr && next != rptr;
		if (!ok) {
			LOG("endpoint 0x%02x: invalid entry at 0x%x: magic 0x%08x size 0x%x", e->index, rptr, le32(qeh), size);
			return;
		}
		if (size > rxScratchSize) {
			afkDrop(e, "entry larger than the scratch buffer", size, rxScratchSize, channel);
		} else {
			memcpy(rxScratch, base + ptr + QEH_SIZE, size);
			if (size < EPIC_HEADER_SIZE) {
				afkDrop(e, "short message", size, 0, channel);
			} else {
				int err = afkHandle(e, channel, rxScratch, size);
				if (err != 0)
					afkDrop(e, err == ENOENT ? "nobody takes it (category, subtype, channel)" :
					    "not understood (category, subtype, channel)", rxScratch[21], le16(rxScratch + 22),
					    channel);
			}
		}
		rptr = next;
		mem_sync();
		put32(e->buf + r->off + r->block, rptr);
		wptr = le32(e->buf + r->off + 2 * r->block);
		mem_sync();
	}
}

/* AFKEndpoint::write_entry(): Ring::write_entry() and the copy; *doorbell announces the entry */
int
SFAop::afkWrite(SFAopEndpoint *e, uint32_t channel, uint32_t type, const uint8_t *hdr, uint32_t hdrLen,
    const uint8_t *data, uint32_t len, uint64_t *doorbell)
{
	const SFAopRing *r = &e->tx;

	if (!r->valid) {
		LOG("endpoint 0x%02x has no transmit ring", e->index);
		return EIO;
	}
	uint8_t *base = e->buf + r->off + 3 * r->block;
	mem_sync();
	uint32_t rptr = le32(e->buf + r->off + r->block);
	uint32_t wptr = le32(e->buf + r->off + 2 * r->block);
	uint32_t payload = hdrLen + len, headNext = 0, next = 0;

	if (!ring_check_ptr(r, rptr) || !ring_check_ptr(r, wptr)) {
		LOG("endpoint 0x%02x: transmit ring pointers out of range: rptr 0x%x wptr 0x%x", e->index, rptr, wptr);
		return EIO;
	}
	if (!ring_end(r, 0, payload, &headNext))
		return EIO;
	bool wrapped = payload > r->size - wptr - QEH_SIZE;
	uint32_t entry = wrapped ? 0 : wptr;
	if (wrapped)
		next = headNext;
	else if (!ring_end(r, entry, payload, &next))
		return EIO;
	if (headNext == 0 || next == rptr || (wptr < rptr && (wrapped || next == 0 || next > rptr)) ||
	    (wrapped && (rptr == 0 || next > rptr))) {
		LOG("endpoint 0x%02x: transmit ring full", e->index);
		return ENOSPC;
	}
	uint8_t qeh[QEH_SIZE];
	put32(qeh, QE_MAGIC1);
	put32(qeh + 4, payload);
	put32(qeh + 8, channel);
	put32(qeh + 12, type);
	if (entry != wptr)
		memcpy(base + wptr, qeh, QEH_SIZE);
	memcpy(base + entry, qeh, QEH_SIZE);
	memcpy(base + entry + QEH_SIZE, hdr, hdrLen);
	memcpy(base + entry + QEH_SIZE + hdrLen, data, len);
	mem_sync();
	put32(e->buf + r->off + 2 * r->block, next);
	*doorbell = (uint64_t)next | (AFK_OPC_SEND << 48);
	return 0;
}

/* AFKEndpoint::recv_message() */
void
SFAop::rxAfk(SFAopEndpoint *e, uint64_t msg)
{
	unsigned opc = (unsigned)(msg >> 48);

	switch (opc) {
	case AFK_OPC_INIT:
		mboxSend(e->index, AFK_MSG_INIT_ACK);
		break;
	case AFK_OPC_GET_BUF: {
		uint32_t size = (uint32_t)((msg & 0xffff0000ULL) >> 16) * AFK_RB_BLOCK_STEP;
		uint64_t at = 0;

		if (e->buf != NULL) {
			LOG("endpoint 0x%02x: GetBuf with a buffer already there", e->index);
			break;
		}
		void *p = windowAlloc(size, &at);
		if (p == NULL) {
			LOG("endpoint 0x%02x: no room for its buffer of 0x%x bytes (0x%x of 0x%x used); not answered",
			    e->index, size, dartUsed, dart.size);
			break;
		}
		/* owned before its address leaves the host */
		e->buf = (uint8_t *)p;
		e->bufSize = size;
		e->iova = at;
		VLOG("endpoint 0x%02x: buffer 0x%x bytes at iova 0x%llx", e->index, size, at);
		mboxSend(e->index, AFK_MSG_GET_BUF_ACK | at);
		break;
	}
	case AFK_OPC_INIT_UNK:
	case AFK_OPC_START_ACK:
		break;
	case AFK_OPC_INIT_RX:
		if (e->rx.valid) {
			LOG("endpoint 0x%02x: InitRX with a receive ring already there", e->index);
			break;
		}
		if (!afkParseRing(e, msg, &e->rx)) {
			LOG("endpoint 0x%02x: InitRX 0x%016llx does not describe a usable ring", e->index, msg);
			break;
		}
		if (e->rx.size > rxScratchSize) {
			uint8_t *bigger = (uint8_t *)IOMalloc(e->rx.size);
			if (bigger != NULL) {
				if (rxScratch != NULL)
					IOFree(rxScratch, rxScratchSize);
				rxScratch = bigger;
				rxScratchSize = e->rx.size;
			}
		}
		if (e->tx.valid)
			mboxSend(e->index, AFK_MSG_START);
		break;
	case AFK_OPC_INIT_TX:
		if (e->tx.valid) {
			LOG("endpoint 0x%02x: InitTX with a transmit ring already there", e->index);
			break;
		}
		if (!afkParseRing(e, msg, &e->tx)) {
			LOG("endpoint 0x%02x: InitTX 0x%016llx does not describe a usable ring", e->index, msg);
			break;
		}
		if (e->rx.valid)
			mboxSend(e->index, AFK_MSG_START);
		break;
	case AFK_OPC_RECV:
		afkRecvRing(e);
		break;
	case AFK_OPC_SHUTDOWN_ACK:
		LOG("endpoint 0x%02x acknowledged a shutdown", e->index);
		break;
	default:
		LOG("endpoint 0x%02x: unknown message 0x%016llx", e->index, msg);
		unknownMessages++;
	}
}

/* AopData::start_afk() */
int
SFAop::startEndpoints(void)
{
	unsigned n = 0;

	for (unsigned i = 0; i < SF_AOP_EP_COUNT; i++) {
		uint8_t no = (uint8_t)(SF_AOP_EP_FIRST + i);

		ep[i].index = no;
		if (!hasEndpoint(no))
			continue;
		/* required_endpoints is empty for T8103: every advertised endpoint has to start */
		if (!startEp(no) || !mboxSend(no, AFK_MSG_INIT)) {
			LOG("endpoint 0x%02x could not be started", no);
			return EIO;
		}
		ep[i].started = true;
		n++;
	}
	LOG("AFK: %u application endpoints started (map 0x%08x)", n, endpoints[1]);
	return n ? 0 : ENOENT;
}

const SFAopService *
SFAop::findService(const char *name) const
{
	for (unsigned i = 0; i < nservices; i++)
		if (strcmp(services[i].name, name) == 0)
			return &services[i];
	return NULL;
}

/* AOP::epic_call(): AFKEndpoint::epic_notify() and the wait for the reply */
int
SFAop::epicCall(const SFAopService *svc, uint16_t subtype, const void *data, uint32_t len, uint32_t *retcode)
{
	if (svc == NULL || svc->ep < SF_AOP_EP_FIRST || svc->ep >= SF_AOP_EP_FIRST + SF_AOP_EP_COUNT)
		return EINVAL;
	SFAopEndpoint *e = &ep[svc->ep - SF_AOP_EP_FIRST];
	unsigned slot;
	uint8_t hdr[EPIC_HEADER_SIZE];
	uint64_t doorbell = 0;

	for (slot = 0; slot < SF_AOP_MAX_CALLS && e->call[slot] != 0; slot++)
		;
	if (slot == SF_AOP_MAX_CALLS) {
		LOG("EPIC: too many calls in flight on endpoint 0x%02x", e->index);
		return EIO;
	}
	epicCalls++;
	bzero(hdr, sizeof(hdr));
	hdr[0] = 2;                              /* version */
	put16(hdr + 1, e->seq);
	put32(hdr + 16, len);                    /* length */
	hdr[20] = 2;                             /* sub_version */
	hdr[21] = EPIC_CATEGORY_NOTIFY;
	put16(hdr + 22, subtype);
	put16(hdr + 24, (uint16_t)(slot + 1));   /* tag */
	int err = afkWrite(e, svc->channel, EPIC_TYPE_NOTIFY, hdr, sizeof(hdr), (const uint8_t *)data, len, &doorbell);
	if (err != 0) {
		epicErrors++;
		return err;
	}
	e->seq++;
	e->call[slot] = 1;
	if (!mboxSend(e->index, doorbell)) {
		/* the entry is in the ring: the next doorbell delivers it, keep the tag reserved */
		e->call[slot] = 3;
		epicErrors++;
		return EIO;
	}
	uint64_t deadline = now_ms() + EPIC_CALL_TIMEOUT_MS;
	for (;;) {
		poll();
		if (e->call[slot] == 2)
			break;
		if (crashed || now_ms() > deadline) {
			e->call[slot] = 3;
			epicTimeouts++;
			LOG("EPIC: call 0x%x on channel %u of endpoint 0x%02x was not answered in %u ms", subtype, svc->channel,
			    e->index, EPIC_CALL_TIMEOUT_MS);
			return ETIMEDOUT;
		}
		IOSleep(1);
	}
	e->call[slot] = 0;
	*retcode = e->retcode[slot];
	return 0;
}

/* ---- boot ---------------------------------------------------------------------------- */

/* AopData::patch_bootargs() with the T8103 values: only the values of the four keys are written */
int
SFAop::patchBootargs(void)
{
	static const struct { uint32_t key; uint64_t value; } patches[] = {
		{ FOURCC('E', 'C', '0', 'p'), T8103_EC0P },
		{ FOURCC('n', 'C', 'a', 'l'), 0 },
		{ FOURCC('a', 'l', 'i', 'g'), T8103_ALIG },
		{ FOURCC('A', 'O', 'P', 't'), T8103_AOPT },
	};
	uint32_t offset = sram.r32(BOOTARGS_OFFSET), size = sram.r32(BOOTARGS_SIZE);
	unsigned entries = 0, patched = 0;

	if (size < 8 || size > 0x10000 || (uint64_t)offset + size > sram.len) {
		LOG("boot arguments at 0x%x, 0x%x bytes: outside the firmware's memory (0x%llx bytes)", offset, size,
		    sram.len);
		return EINVAL;
	}
	volatile uint8_t *args = sram.va + offset;
	for (uint32_t idx = 0; idx + 8 <= size;) {
		/* entries are not aligned (a value may be one byte long): device memory is read bytewise */
		uint32_t key = dev_le32(args + idx), len = dev_le32(args + idx + 4);

		idx += 8;
		if (len > size - idx) {
			LOG("boot argument %c%c%c%c of 0x%x bytes overruns the list", (char)(key >> 24), (char)(key >> 16),
			    (char)(key >> 8), (char)key, len);
			return EINVAL;
		}
		entries++;
		for (unsigned p = 0; p < sizeof(patches) / sizeof(patches[0]); p++) {
			if (patches[p].key != key)
				continue;
			uint64_t before = 0;
			if (len > 8) {
				LOG("boot argument %c%c%c%c has %u bytes; not changed", (char)(key >> 24), (char)(key >> 16),
				    (char)(key >> 8), (char)key, len);
				return EINVAL;
			}
			for (uint32_t b = 0; b < len; b++) {
				before |= (uint64_t)args[idx + b] << (8 * b);
				args[idx + b] = (uint8_t)(patches[p].value >> (8 * b));
			}
			OSSynchronizeIO();
			LOG("boot argument %c%c%c%c (%u bytes): 0x%llx -> 0x%llx", (char)(key >> 24), (char)(key >> 16),
			    (char)(key >> 8), (char)key, len, before, patches[p].value);
			patched++;
			break;
		}
		idx += len;
	}
	LOG("boot arguments at 0x%x, 0x%x bytes: %u entries, %u set", offset, size, entries, patched);
	return 0;
}

/* m1n1 dapf_init("/arm-io/dart-aop", 1): dapf_init_t8020() */
int
SFAop::dapfInit(void)
{
	IORegistryEntry *node = IORegistryEntry::fromPath("/arm-io/dart-aop", gIODTPlane);
	OSData *filter = node ? OSDynamicCast(OSData, node->getProperty("filter-data-instance-0")) : NULL;
	SFMmio regs;
	int ret = ENXIO;

	if (filter != NULL && filter->getLength() != 0 && filter->getLength() % 24 == 0 &&
	    regs.mapNode("/arm-io/dart-aop", 1)) {
		const uint8_t *c = (const uint8_t *)filter->getBytesNoCopy();
		unsigned count = filter->getLength() / 24, same = 0;

		for (unsigned i = 0; i < count; i++, c += 24) {
			uint32_t at = i * 0x40, r0 = ((uint32_t)c[17] << 4) | c[18], r4 = le32(c + 20);
			uint32_t startLo = le32(c), startHi = le32(c + 4), endLo = le32(c + 8), endHi = le32(c + 12);

			if (regs.r32(at) == r0 && regs.r32(at + 4) == r4 && regs.r32(at + 8) == startLo &&
			    regs.r32(at + 0xc) == startHi && regs.r32(at + 0x10) == endLo && regs.r32(at + 0x14) == endHi)
				same++;
			regs.w32(at + 0x04, r4);
			regs.w32(at + 0x08, startLo);
			regs.w32(at + 0x0c, startHi);
			regs.w32(at + 0x10, endLo);
			regs.w32(at + 0x14, endHi);
			regs.w32(at + 0x00, r0);
		}
		LOG("dart-aop address filter at 0x%llx: %u entries written (%u already had these values)", regs.phys,
		    count, same);
		ret = 0;
	} else {
		LOG("dart-aop: no usable filter-data-instance-0 / reg 1");
	}
	regs.unmap();
	OSSafeReleaseNULL(node);
	return ret;
}

int
SFAop::boot(uint64_t windowIova, uint32_t windowBytes, char *why, size_t whyLen)
{
	int err;

#define FAIL(code, ...) do { snprintf(why, whyLen, __VA_ARGS__); LOG("%s", why); return (code); } while (0)
	for (unsigned i = 0; i < SF_AOP_EP_COUNT; i++)
		ep[i].index = (uint8_t)(SF_AOP_EP_FIRST + i);
	if (!asc.mapNode("/arm-io/aop", 0) || !sram.mapNode("/arm-io/aop", 2))
		FAIL(ENXIO, "the registers of /arm-io/aop could not be mapped");
	if (asc.len < ASC_MBOX + 0x1000 || sram.len < 0x1000)
		FAIL(ENXIO, "/arm-io/aop has unexpected register sizes");
	uint32_t control = asc.r32(ASC_CPU_CONTROL);
	LOG("asc 0x%llx (0x%llx bytes), firmware memory 0x%llx (0x%llx bytes): cpu_control=0x%08x cpu_status=0x%08x "
	    "a2i=0x%08x i2a=0x%08x", asc.phys, asc.len, sram.phys, sram.len, control, asc.r32(ASC_CPU_STATUS),
	    asc.r32(ASC_MBOX + MBOX_A2I_CONTROL), asc.r32(ASC_MBOX + MBOX_I2A_CONTROL));
	if (control & ASC_CPU_CONTROL_RUN)
		FAIL(EBUSY, "the AOP is already running (cpu_control 0x%08x): not started by this driver, left alone", control);
	if (!(asc.r32(ASC_MBOX + MBOX_I2A_CONTROL) & MBOX_CONTROL_EMPTY))
		FAIL(EBUSY, "the AOP's mailbox holds messages before it was started; left alone");

	if ((err = dapfInit()) != 0)
		FAIL(err, "the address filter of dart-aop could not be programmed");
	if ((err = dart.attach("/arm-io/dart-aop", 0, windowIova, windowBytes)) != 0)
		FAIL(err, "dart-aop stream 0 could not be attached (%d)", err);
	if ((err = patchBootargs()) != 0)
		FAIL(err, "the firmware's boot arguments could not be completed (%d)", err);

	asc.w32(ASC_CPU_CONTROL, asc.r32(ASC_CPU_CONTROL) | ASC_CPU_CONTROL_RUN);
	LOG("coprocessor started: cpu_control=0x%08x", asc.r32(ASC_CPU_CONTROL));

	/* apple_rtkit_wake(): IOP power INIT, then the boot handshake */
	uint64_t t0 = now_ms();
	if (!mgmtSend(MGMT_SET_IOP_PWR_STATE, PWR_STATE_INIT))
		FAIL(EIO, "the IOP power request could not be sent");
	if (!waitFlag(&epmapDone, RTKIT_WAIT_MS))
		FAIL(ETIMEDOUT, "RTKit: endpoint discovery failed (hello %d, %u ms, cpu_status 0x%08x)", helloSeen,
		    (unsigned)(now_ms() - t0), asc.r32(ASC_CPU_STATUS));
	if (!waitFlag(&iopAck, RTKIT_WAIT_MS))
		FAIL(ETIMEDOUT, "RTKit: no IOP power acknowledgment");
	apAck = false;
	if (!mgmtSend(MGMT_SET_AP_PWR_STATE, PWR_STATE_ON))
		FAIL(EIO, "the AP power request could not be sent");
	if (!waitFlag(&apAck, RTKIT_WAIT_MS))
		FAIL(ETIMEDOUT, "RTKit: no AP power acknowledgment");
	if (apPower != PWR_STATE_ON)
		FAIL(EINVAL, "RTKit: AP power state is 0x%x", apPower);
	LOG("RTKit: booted in %u ms: protocol %u, IOP power 0x%x, AP power 0x%x, endpoints 0x%08x 0x%08x", (unsigned)(now_ms() - t0),
	    version, iopPower, apPower, endpoints[0], endpoints[1]);
	dart.check("after boot");
	return 0;
#undef FAIL
}
