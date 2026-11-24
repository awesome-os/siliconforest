// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal polled Apple SMC client for SiliconForest: ASC mailbox, RTKit management
 * handshake, SMC endpoint, key read/write. No interrupts; every wait is bounded.
 *
 * This file as a whole is distributed under GPL-2.0, like the rest of the kext.
 *
 * The mailbox access, the RTKit boot/quiesce sequence, the system endpoint handling and
 * the SMC endpoint protocol are ported from m1n1 src/asc.c, src/rtkit.c, src/smc.c and
 * proxyclient/m1n1/fw/smc.py, fw/asc/mgmt.py, hw/asc.py, which carry this notice:
 *
 *      SPDX-License-Identifier: MIT
 *      Copyright The Asahi Linux Contributors
 *
 *      Permission is hereby granted, free of charge, to any person obtaining
 *      a copy of this software and associated documentation files (the
 *      "Software"), to deal in the Software without restriction, including
 *      without limitation the rights to use, copy, modify, merge, publish,
 *      distribute, sublicense, and/or sell copies of the Software, and to
 *      permit persons to whom the Software is furnished to do so, subject to
 *      the following conditions:
 *
 *      The above copyright notice and this permission notice shall be
 *      included in all copies or substantial portions of the Software.
 *
 *      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *      EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *      MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *      IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *      CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *      TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *      SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Session shape (m1n1 smc_init() ... smc_shutdown(), Python start()/start_ep()/stop()):
 *   set CPU_CONTROL.RUN, send IOP power 0x220, answer HELLO, answer the endpoint map,
 *   start the system endpoints, wait for IOP power 0x20, send AP power 0x20, start
 *   endpoint 0x20, send SMC INITIALIZE, take the shared-memory address, run key
 *   commands, then AP power 0x10 and IOP power 0x10.
 *
 * Every on-SoC register and the SMC shared memory are mapped non-posted
 * (kIOMapInhibitCache).
 */
#include "SFAppleSMC.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <libkern/OSAtomic.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>

#define LOG(fmt, ...) IOLog("SFAppleSMC: " fmt "\n", ##__VA_ARGS__)

#define BIT(n) (1u << (n))
#define BIT64(n) (1ULL << (n))
#define MASK64(h, l) (((~0ULL) >> (63 - (h))) & ((~0ULL) << (l)))

#define SOC_PAGE 0x4000ULL

/* ---- m1n1 src/asc.c --------------------------------------------------------- */

#define ASC_CPU_CONTROL        0x44
#define ASC_CPU_CONTROL_START  0x10
#define ASC_CPU_STATUS         0x48
#define ASC_CPU_STATUS_STOPPED BIT(1)
#define ASC_CPU_STATUS_IDLE    BIT(5)

#define ASC_MBOX_OFFSET 0x8000 /* asc->base = base + 0x8000 */

#define ASC_MBOX_CONTROL_FULL  BIT(16)
#define ASC_MBOX_CONTROL_EMPTY BIT(17)

#define ASC_MBOX_A2I_CONTROL 0x110
#define ASC_MBOX_A2I_SEND0   0x800
#define ASC_MBOX_A2I_SEND1   0x808
#define ASC_MBOX_I2A_CONTROL 0x114
#define ASC_MBOX_I2A_RECV0   0x830
#define ASC_MBOX_I2A_RECV1   0x838

#define ASC_SEND_TIMEOUT_US 200000 /* m1n1: "A2I mailbox full for 200ms" */

/* ---- m1n1 src/rtkit.c ------------------------------------------------------- */

#define RTKIT_EP_MGMT     0
#define RTKIT_EP_CRASHLOG 1
#define RTKIT_EP_SYSLOG   2
#define RTKIT_EP_DEBUG    3
#define RTKIT_EP_IOREPORT 4
#define RTKIT_EP_OSLOG    8

#define MGMT_TYPE_SHIFT 52
#define MGMT_TYPE(m)    ((UInt32)(((m) >> MGMT_TYPE_SHIFT) & 0xff)) /* GENMASK(59, 52) */
#define MGMT_PWR_STATE(m) ((UInt32)((m) & 0xffff))

#define MSG_BUFFER_REQUEST         1
#define MSG_BUFFER_REQUEST_SIZE(m) ((UInt32)(((m) >> 44) & 0xff)) /* GENMASK(51, 44), 4K pages */
#define MSG_BUFFER_REQUEST_IOVA(m) ((m) & MASK64(41, 0))

#define MSG_SYSLOG_INIT 8
#define MSG_SYSLOG_LOG  5

#define MGMT_MSG_HELLO             1
#define MGMT_MSG_HELLO_ACK         2
#define MGMT_MSG_START_EP          5
#define MGMT_MSG_START_EP_FLAG     BIT64(1)
#define MGMT_MSG_IOP_PWR_STATE     6
#define MGMT_MSG_IOP_PWR_STATE_ACK 7
#define MGMT_MSG_EPMAP             8
#define MGMT_MSG_EPMAP_DONE        BIT64(51)
#define MGMT_MSG_EPMAP_REPLY_MORE  BIT64(0)
#define MGMT_MSG_AP_PWR_STATE      0xb

#define RTKIT_MIN_VERSION 11
#define RTKIT_MAX_VERSION 12

#define RTKIT_POWER_QUIESCED 0x10
#define RTKIT_POWER_ON       0x20
#define RTKIT_POWER_INIT     0x220

/* proxyclient m1n1/fw/asc/kdebug.py: KDebugStart, TYPE = bits 55:48 = 8 */
#define KDEBUG_MSG_START (8ULL << 48)

/* ---- m1n1 src/smc.c, proxyclient m1n1/fw/smc.py ----------------------------- */

#define SMC_ENDPOINT 0x20

#define SMC_READ_KEY     0x10
#define SMC_WRITE_KEY    0x11
#define SMC_GET_KEY_INFO 0x13
#define SMC_INITIALIZE   0x17
#define SMC_NOTIFICATION 0x18

#define SMC_MSG_ID_SHIFT   12 /* GENMASK(15, 12) */
#define SMC_MSG_SIZE_SHIFT 16 /* GENMASK(23, 16) */
#define SMC_MSG_KEY_SHIFT  32 /* GENMASK(63, 32) */

#define SMC_RESULT_RESULT(m) ((UInt32)((m) & 0xff))
#define SMC_RESULT_ID(m)     ((UInt32)(((m) >> 12) & 0xf))
#define SMC_RESULT_SIZE(m)   ((UInt32)(((m) >> 16) & 0xffff))
#define SMC_RESULT_VALUE(m)  ((UInt32)((m) >> 32))

#define SMC_SHMEM_MAP_LEN 0x100 /* key data is at most 255 bytes */

/* ---- waits (all bounded) ----------------------------------------------------- */

#define WAIT_HELLO_US   1000000 /* m1n1 rtkit_boot: USEC_PER_SEC for HELLO */
#define WAIT_BOOT_US    3000000 /* Python StandardASC.start(): wait_boot(3) */
#define WAIT_AP_US      1000000
#define WAIT_SHMEM_US   1000000
#define WAIT_CMD_US     2000000
#define WAIT_QUIESCE_US 1000000 /* m1n1 rtkit_switch_power_state default */
#define MAX_MESSAGES_PER_WAIT 512

namespace {

/* Session serialisation and the once-resolved /arm-io/smc reg 0. */
volatile UInt32 gBusy;
UInt64 gSmcBase;
UInt64 gSmcRegLen;
bool gSmcResolved;

struct Window {
    IOMemoryDescriptor *md;
    IOMemoryMap *map;
    volatile UInt8 *va;
    UInt64 phys;

    Window() : md(NULL), map(NULL), va(NULL), phys(0) {}
    ~Window() { unmap(); }

    void unmap()
    {
        OSSafeReleaseNULL(map);
        OSSafeReleaseNULL(md);
        va = NULL;
    }

    /* Non-posted device mapping of [p, p + l). */
    bool mapPhys(const char *name, UInt64 p, UInt64 l)
    {
        unmap();
        UInt64 start = p & ~(SOC_PAGE - 1);
        UInt64 span = ((p + l + SOC_PAGE - 1) & ~(SOC_PAGE - 1)) - start;
        md = IODeviceMemory::withRange((IOPhysicalAddress)start, (IOPhysicalLength)span);
        map = md ? md->map(kIOMapInhibitCache) : NULL;
        if (!map || !map->getVirtualAddress()) {
            LOG("could not map %s at 0x%llx len 0x%llx", name, (unsigned long long)p, (unsigned long long)l);
            unmap();
            return false;
        }
        va = (volatile UInt8 *)map->getVirtualAddress() + (p - start);
        phys = p;
        return true;
    }

    UInt32 r32(UInt64 off) const
    {
        UInt32 v = OSReadLittleInt32((volatile void *)va, off);
        OSSynchronizeIO();
        return v;
    }
    void w32(UInt64 off, UInt32 v) const
    {
        OSWriteLittleInt32((volatile void *)va, off, v);
        OSSynchronizeIO();
    }
    UInt64 r64(UInt64 off) const
    {
        UInt64 v = OSReadLittleInt64((volatile void *)va, off);
        OSSynchronizeIO();
        return v;
    }
    void w64(UInt64 off, UInt64 v) const
    {
        OSWriteLittleInt64((volatile void *)va, off, v);
        OSSynchronizeIO();
    }
};

/* 10 us busy steps for the first 2 ms, then 1 ms sleeps (as poll32 in the link code). */
void waitStep(UInt32 *waited)
{
    if (*waited < 2000) {
        IODelay(10);
        *waited += 10;
    } else {
        IOSleep(1);
        *waited += 1000;
    }
}

bool nodeIsCompatible(IORegistryEntry *entry, const char *want)
{
    OSData *d = OSDynamicCast(OSData, entry->getProperty("compatible"));
    if (!d)
        return false;
    const char *p = (const char *)d->getBytesNoCopy();
    size_t left = d->getLength(), n = strlen(want);
    while (left) {
        size_t l = strnlen(p, left);
        if (l == n && !memcmp(p, want, n))
            return true;
        if (l >= left)
            break;
        p += l + 1;
        left -= l + 1;
    }
    return false;
}

enum Cond { kCondHello, kCondBooted, kCondApOn, kCondShmem, kCondCmdDone, kCondApQuiesced, kCondIopQuiesced };

struct Smc {
    Window cpu, mbox, shm;
    UInt32 options;

    /* RTKit management state */
    bool talked;    /* something was sent; a quiesce is attempted on close */
    bool helloSeen;
    bool epmapDone;
    bool failed;    /* crash or a system message that cannot be answered */
    UInt32 sysEps;  /* bitmap of endpoints 0..31 from the endpoint map */
    bool smcEpSeen; /* endpoint 0x20 in the endpoint map */
    UInt32 iopPower, apPower;
    bool crashBuf, syslogBuf, ioreportBuf;

    /* SMC endpoint state */
    bool shmemValid;
    UInt64 shmem;
    UInt32 msgid;
    bool pending;
    UInt32 pendingId;
    UInt64 result;

    Smc()
        : options(0), talked(false), helloSeen(false), epmapDone(false), failed(false), sysEps(0), smcEpSeen(false),
          iopPower(0), apPower(0), crashBuf(false), syslogBuf(false), ioreportBuf(false), shmemValid(false), shmem(0),
          msgid(0), pending(false), pendingId(0), result(0)
    {
    }

    /* ---- m1n1 asc.c ---- */

    bool recv(UInt64 *msg0, UInt32 *ep)
    {
        for (;;) {
            if (mbox.r32(ASC_MBOX_I2A_CONTROL) & ASC_MBOX_CONTROL_EMPTY)
                return false;
            UInt64 m0 = mbox.r64(ASC_MBOX_I2A_RECV0);
            UInt32 m1 = (UInt32)mbox.r64(ASC_MBOX_I2A_RECV1);
            if (m1 >= 0x100) {
                LOG("< message for invalid endpoint 0x%x >= 0x100 dropped (0x%016llx)", m1, (unsigned long long)m0);
                continue;
            }
            *msg0 = m0;
            *ep = m1;
            LOG("< ep 0x%02x 0x%016llx", m1, (unsigned long long)m0);
            return true;
        }
    }

    bool send(UInt32 ep, UInt64 msg0)
    {
        UInt32 waited = 0, ctrl;
        while ((ctrl = mbox.r32(ASC_MBOX_A2I_CONTROL)) & ASC_MBOX_CONTROL_FULL) {
            if (waited >= ASC_SEND_TIMEOUT_US) {
                LOG("A2I mailbox full for 200 ms (control 0x%08x); ep 0x%02x 0x%016llx NOT sent", ctrl, ep,
                    (unsigned long long)msg0);
                return false;
            }
            waitStep(&waited);
        }
        LOG("> ep 0x%02x 0x%016llx", ep, (unsigned long long)msg0);
        mbox.w64(ASC_MBOX_A2I_SEND0, msg0);
        mbox.w64(ASC_MBOX_A2I_SEND1, (UInt64)ep);
        talked = true;
        return true;
    }

    void logMailbox(const char *when)
    {
        UInt32 control = cpu.r32(ASC_CPU_CONTROL), status = cpu.r32(ASC_CPU_STATUS);
        UInt32 a2i = mbox.r32(ASC_MBOX_A2I_CONTROL), i2a = mbox.r32(ASC_MBOX_I2A_CONTROL);
        LOG("%s: cpu control 0x%08x (%s) status 0x%08x (%s%s) mailbox A2I 0x%08x I2A 0x%08x (%s); hello %d epmap %d "
            "iop 0x%x ap 0x%x",
            when, control, (control & ASC_CPU_CONTROL_START) ? "run" : "not started", status,
            (status & ASC_CPU_STATUS_STOPPED) ? "stopped" : "running", (status & ASC_CPU_STATUS_IDLE) ? ", idle" : "",
            a2i, i2a, (i2a & ASC_MBOX_CONTROL_EMPTY) ? "empty" : "message pending", helloSeen, epmapDone, iopPower,
            apPower);
    }

    /* ---- m1n1 rtkit.c ---- */

    bool startEp(UInt32 ep, const char *name)
    {
        LOG("starting endpoint 0x%02x (%s)", ep, name);
        return send(RTKIT_EP_MGMT,
                    ((UInt64)MGMT_MSG_START_EP << MGMT_TYPE_SHIFT) | MGMT_MSG_START_EP_FLAG | ((UInt64)ep << 32));
    }

    /*
     * rtkit_handle_buffer_request() for an "sram" RTKit (what m1n1 uses for the SMC): the
     * coprocessor names its own buffer; it is recorded and nothing is sent back.
     */
    void bufferRequest(const char *name, UInt64 msg, bool *have)
    {
        UInt64 addr = MSG_BUFFER_REQUEST_IOVA(msg);
        UInt32 pages = MSG_BUFFER_REQUEST_SIZE(msg);
        if (!addr) {
            LOG("%s buffer request for 0x%x bytes without an address: the SMC is expected to preallocate its "
                "buffers (m1n1: \"SRAM buffers needs to be provided by the IOP\"); giving up",
                name, pages << 12);
            failed = true;
            return;
        }
        *have = true;
        LOG("%s buffer preallocated by the SMC at 0x%llx, 0x%x bytes; no reply needed", name, (unsigned long long)addr,
            pages << 12);
    }

    void handleMgmt(UInt64 msg)
    {
        switch (MGMT_TYPE(msg)) {
        case MGMT_MSG_HELLO: {
            UInt32 minVer = (UInt32)(msg & 0xffff), maxVer = (UInt32)((msg >> 16) & 0xffff);
            UInt32 want = maxVer < RTKIT_MAX_VERSION ? maxVer : RTKIT_MAX_VERSION;
            if (minVer > RTKIT_MAX_VERSION || maxVer < RTKIT_MIN_VERSION) {
                LOG("HELLO: SMC supports RTKit versions %u .. %u, this client %u .. %u: no overlap", minVer, maxVer,
                    RTKIT_MIN_VERSION, RTKIT_MAX_VERSION);
                failed = true;
                return;
            }
            LOG("HELLO: supported versions %u .. %u; acknowledging version %u", minVer, maxVer, want);
            /* a HELLO starts a fresh handshake */
            helloSeen = true;
            epmapDone = false;
            sysEps = 0;
            smcEpSeen = false;
            iopPower = apPower = 0;
            crashBuf = syslogBuf = ioreportBuf = false;
            if (!send(RTKIT_EP_MGMT, ((UInt64)MGMT_MSG_HELLO_ACK << MGMT_TYPE_SHIFT) | ((UInt64)want << 16) | want))
                failed = true;
            break;
        }
        case MGMT_MSG_EPMAP: {
            UInt32 bitmap = (UInt32)msg, base = (UInt32)((msg >> 32) & 7);
            bool last = (msg & MGMT_MSG_EPMAP_DONE) != 0;
            for (unsigned i = 0; i < 32; i++) {
                if (!(bitmap & BIT(i)))
                    continue;
                UInt32 ep = 32 * base + i;
                if (ep < 32)
                    sysEps |= BIT(ep);
                if (ep == SMC_ENDPOINT)
                    smcEpSeen = true;
            }
            LOG("endpoint map: base %u bitmap 0x%08x%s (endpoints 0x%02x..0x%02x)", base, bitmap, last ? ", last" : "",
                32 * base, 32 * base + 31);
            UInt64 reply = ((UInt64)MGMT_MSG_EPMAP << MGMT_TYPE_SHIFT) | ((UInt64)base << 32);
            reply |= last ? MGMT_MSG_EPMAP_DONE : MGMT_MSG_EPMAP_REPLY_MORE;
            if (!send(RTKIT_EP_MGMT, reply)) {
                failed = true;
                return;
            }
            if (!last)
                break;
            /* start all required system endpoints, in m1n1 rtkit_boot order */
            bool ok = true;
            if (ok && (sysEps & BIT(RTKIT_EP_DEBUG))) {
                ok = startEp(RTKIT_EP_DEBUG, "debug");
                if (ok && (options & kSFAppleSMCKDebugStart)) {
                    LOG("sending kdebug start (sfapciesmc bit 2, as the m1n1 Python client)");
                    ok = send(RTKIT_EP_DEBUG, KDEBUG_MSG_START);
                }
            }
            if (ok && (sysEps & BIT(RTKIT_EP_CRASHLOG)))
                ok = startEp(RTKIT_EP_CRASHLOG, "crashlog");
            if (ok && (sysEps & BIT(RTKIT_EP_SYSLOG)))
                ok = startEp(RTKIT_EP_SYSLOG, "syslog");
            if (ok && (sysEps & BIT(RTKIT_EP_IOREPORT)))
                ok = startEp(RTKIT_EP_IOREPORT, "ioreport");
            if (ok && (sysEps & BIT(RTKIT_EP_OSLOG)))
                ok = startEp(RTKIT_EP_OSLOG, "oslog");
            UInt32 unknown = sysEps & ~(BIT(RTKIT_EP_MGMT) | BIT(RTKIT_EP_CRASHLOG) | BIT(RTKIT_EP_SYSLOG) |
                                        BIT(RTKIT_EP_DEBUG) | BIT(RTKIT_EP_IOREPORT) | BIT(RTKIT_EP_OSLOG));
            if (unknown)
                LOG("unknown system endpoints in the map (bitmap 0x%08x), not started", unknown);
            if (!ok)
                failed = true;
            epmapDone = true;
            break;
        }
        case MGMT_MSG_IOP_PWR_STATE_ACK:
            iopPower = MGMT_PWR_STATE(msg);
            LOG("IOP power state is now 0x%x", iopPower);
            break;
        case MGMT_MSG_AP_PWR_STATE:
            apPower = MGMT_PWR_STATE(msg);
            LOG("AP power state is now 0x%x", apPower);
            break;
        default:
            LOG("unknown management message type 0x%x (0x%016llx), ignored", MGMT_TYPE(msg), (unsigned long long)msg);
        }
    }

    /* m1n1 smc_handle_msg() */
    void handleSmc(UInt64 msg)
    {
        if (!shmemValid) {
            shmem = msg;
            shmemValid = true;
            return;
        }
        UInt32 res = SMC_RESULT_RESULT(msg), id = SMC_RESULT_ID(msg);
        if (res == SMC_NOTIFICATION) {
            LOG("SMC notification 0x%08x, ignored", SMC_RESULT_VALUE(msg));
            return;
        }
        if (!pending || id != pendingId) {
            LOG("SMC reply for id %u while %s; ignored", id, pending ? "waiting for another id" : "no command is pending");
            return;
        }
        result = msg;
        pending = false;
    }

    /* m1n1 rtkit_recv() dispatch; application endpoints are handled here too */
    void handle(UInt32 ep, UInt64 msg)
    {
        UInt32 type = MGMT_TYPE(msg);
        switch (ep) {
        case RTKIT_EP_MGMT:
            handleMgmt(msg);
            break;
        case RTKIT_EP_SYSLOG:
            if (type == MSG_BUFFER_REQUEST) {
                bufferRequest("syslog", msg, &syslogBuf);
            } else if (type == MSG_SYSLOG_INIT) {
                LOG("syslog init: %u entries of %u bytes", (UInt32)(msg & 0xffff), (UInt32)((msg >> 24) & 0xffff));
            } else if (type == MSG_SYSLOG_LOG) {
                /* the text is not read; the message must be sent back as the ack */
                if (!send(ep, msg))
                    LOG("failed to ack syslog entry %u", (UInt32)(msg & 0xff));
            } else {
                LOG("unknown syslog message type 0x%x, ignored", type);
            }
            break;
        case RTKIT_EP_CRASHLOG:
            if (type != MSG_BUFFER_REQUEST) {
                LOG("unknown crashlog message type 0x%x, ignored", type);
            } else if (!crashBuf) {
                bufferRequest("crashlog", msg, &crashBuf);
            } else {
                LOG("the SMC firmware CRASHED (second crashlog message 0x%016llx); the crash log is not read",
                    (unsigned long long)msg);
                failed = true;
            }
            break;
        case RTKIT_EP_IOREPORT:
            if (type == MSG_BUFFER_REQUEST) {
                bufferRequest("ioreport", msg, &ioreportBuf);
            } else if (type == 0x8 || type == 0xc) {
                /* m1n1: unknown but must be ACKed */
                if (!send(ep, msg))
                    LOG("failed to ack ioreport message type 0x%x", type);
            } else {
                LOG("unknown ioreport message type 0x%x, ignored", type);
            }
            break;
        case SMC_ENDPOINT:
            handleSmc(msg);
            break;
        default:
            LOG("message for unhandled endpoint 0x%02x (0x%016llx), ignored", ep, (unsigned long long)msg);
        }
    }

    bool cond(Cond c) const
    {
        switch (c) {
        case kCondHello:
            return helloSeen;
        case kCondBooted:
            return helloSeen && epmapDone && iopPower == RTKIT_POWER_ON;
        case kCondApOn:
            return apPower == RTKIT_POWER_ON;
        case kCondShmem:
            return shmemValid;
        case kCondCmdDone:
            return !pending;
        case kCondApQuiesced:
            return apPower == RTKIT_POWER_QUIESCED;
        case kCondIopQuiesced:
            return iopPower == RTKIT_POWER_QUIESCED;
        }
        return false;
    }

    /* Service the mailbox until the condition holds. Bounded by time and message count. */
    bool waitFor(Cond c, UInt32 timeoutUs, const char *what)
    {
        UInt32 waited = 0;
        unsigned handled = 0;
        for (;;) {
            UInt64 msg;
            UInt32 ep;
            while (recv(&msg, &ep)) {
                handle(ep, msg);
                if (++handled > MAX_MESSAGES_PER_WAIT) {
                    LOG("more than %u messages while waiting for %s; giving up", MAX_MESSAGES_PER_WAIT, what);
                    failed = true;
                    break;
                }
            }
            if (failed) {
                logMailbox("failed");
                return false;
            }
            if (cond(c))
                return true;
            if (waited >= timeoutUs) {
                LOG("TIMEOUT after %u ms waiting for %s", timeoutUs / 1000, what);
                logMailbox("at timeout");
                return false;
            }
            waitStep(&waited);
        }
    }

    /* Map the ASC, boot/wake RTKit, start the SMC endpoint, get the shared memory. */
    IOReturn open(UInt32 opts)
    {
        options = opts;

        if (!gSmcResolved) {
            IORegistryEntry *node = IORegistryEntry::fromPath("/arm-io/smc", gIODTPlane);
            if (!node) {
                LOG("no /arm-io/smc in the device tree");
                return kIOReturnNotFound;
            }
            if (nodeIsCompatible(node, "iop,ascwrap-v8")) {
                LOG("/arm-io/smc is iop,ascwrap-v8 (post-office mailbox); not supported by this client");
                node->release();
                return kIOReturnUnsupported;
            }
            /* The registry owns the returned array: used at once, never retained or released,
             * and resolved only once per boot (gSmcResolved). */
            OSArray *regs = IODTResolveAddressing(node, "reg", NULL);
            IOMemoryDescriptor *d = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
            if (d) {
                gSmcBase = d->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
                gSmcRegLen = d->getLength();
            }
            node->release();
            if (!d || !gSmcBase) {
                LOG("could not resolve /arm-io/smc reg 0");
                return kIOReturnNoResources;
            }
            gSmcResolved = true;
        }
        LOG("/arm-io/smc reg 0: 0x%llx len 0x%llx; ASC CPU control at +0x%x, mailbox at +0x%x", (unsigned long long)gSmcBase,
            (unsigned long long)gSmcRegLen, ASC_CPU_CONTROL, ASC_MBOX_OFFSET);
        if (gSmcRegLen < ASC_MBOX_OFFSET + ASC_MBOX_I2A_RECV1 + 8)
            LOG("note: reg 0 is shorter than the 0x%x m1n1 uses from this base; going on as m1n1 does",
                ASC_MBOX_OFFSET + ASC_MBOX_I2A_RECV1 + 8);

        if (!cpu.mapPhys("ASC CPU control", gSmcBase, 0x100) ||
            !mbox.mapPhys("ASC mailbox", gSmcBase + ASC_MBOX_OFFSET, 0x1000))
            return kIOReturnNoMemory;

        LOG("first read of the ASC CPU control block at 0x%llx", (unsigned long long)(gSmcBase + ASC_CPU_CONTROL));
        UInt32 control = cpu.r32(ASC_CPU_CONTROL), status = cpu.r32(ASC_CPU_STATUS);
        LOG("cpu control 0x%08x status 0x%08x; first read of the mailbox block at 0x%llx", control, status,
            (unsigned long long)(gSmcBase + ASC_MBOX_OFFSET + ASC_MBOX_A2I_CONTROL));
        logMailbox("mailbox state before");

        /* rtkit_boot(): boot the IOP if it isn't already (m1n1 sets the bit unconditionally) */
        cpu.w32(ASC_CPU_CONTROL, control | ASC_CPU_CONTROL_START);
        LOG("cpu control RUN %s; sending IOP power state 0x%x (wake/start)",
            (control & ASC_CPU_CONTROL_START) ? "was already set (written again as m1n1 does)" : "set", RTKIT_POWER_INIT);
        /* can be sent unconditionally to wake up a possibly sleeping IOP */
        if (!send(RTKIT_EP_MGMT, ((UInt64)MGMT_MSG_IOP_PWR_STATE << MGMT_TYPE_SHIFT) | RTKIT_POWER_INIT))
            return kIOReturnNotReady;

        if (!waitFor(kCondHello, WAIT_HELLO_US, "HELLO"))
            return kIOReturnTimeout;
        if (!waitFor(kCondBooted, WAIT_BOOT_US, "the endpoint map and IOP power state 0x20"))
            return kIOReturnTimeout;
        if (!smcEpSeen) {
            LOG("the endpoint map has no endpoint 0x%02x; this is not an SMC", SMC_ENDPOINT);
            return kIOReturnUnsupported;
        }

        /* this enables syslog (m1n1) */
        if (!send(RTKIT_EP_MGMT, ((UInt64)MGMT_MSG_AP_PWR_STATE << MGMT_TYPE_SHIFT) | RTKIT_POWER_ON))
            return kIOReturnNotReady;
        /* m1n1 rtkit_boot() does not wait for this ack; the Python client does. */
        if (!waitFor(kCondApOn, WAIT_AP_US, "AP power state 0x20")) {
            if (failed)
                return kIOReturnIOError;
            LOG("no AP power ack; going on as m1n1 rtkit_boot() does");
        }
        LOG("RTKit startup complete (iop 0x%x ap 0x%x)", iopPower, apPower);

        /* smc_init() */
        if (!startEp(SMC_ENDPOINT, "smc"))
            return kIOReturnNotReady;
        LOG("sending SMC INITIALIZE");
        if (!send(SMC_ENDPOINT, (UInt64)SMC_INITIALIZE | ((UInt64)(msgid++ & 0xf) << SMC_MSG_ID_SHIFT)))
            return kIOReturnNotReady;
        if (!waitFor(kCondShmem, WAIT_SHMEM_US, "the SMC shared memory address"))
            return kIOReturnTimeout;
        LOG("SMC shared memory at 0x%llx", (unsigned long long)shmem);
        if (!shmem || (shmem >> 42)) {
            LOG("that is not a usable physical address");
            return kIOReturnBadArgument;
        }
        if (!shm.mapPhys("SMC shared memory", shmem, SMC_SHMEM_MAP_LEN))
            return kIOReturnNoMemory;
        return kIOReturnSuccess;
    }

    /* m1n1 smc_cmd(): returns the SMC result byte in *res when the SMC answered */
    IOReturn cmd(UInt64 message, const char *what, UInt32 *res)
    {
        UInt32 id = msgid++ & 0xf;
        pending = true;
        pendingId = id;
        result = 0;
        if (!send(SMC_ENDPOINT, message | ((UInt64)id << SMC_MSG_ID_SHIFT))) {
            pending = false;
            return kIOReturnNotReady;
        }
        if (!waitFor(kCondCmdDone, WAIT_CMD_US, what)) {
            pending = false;
            return failed ? kIOReturnIOError : kIOReturnTimeout;
        }
        if (res)
            *res = SMC_RESULT_RESULT(result);
        return SMC_RESULT_RESULT(result) ? kIOReturnDeviceError : kIOReturnSuccess;
    }

    void shmRead(UInt8 *dst, UInt32 len)
    {
        for (UInt32 i = 0; i < len; i++)
            dst[i] = shm.va[i];
        OSSynchronizeIO();
    }

    /* Python SMCEndpoint.read() */
    IOReturn readKey(UInt32 key, void *buf, UInt32 len, UInt32 *res)
    {
        UInt32 r = 0;
        if (!shm.va || !len || len > 255)
            return kIOReturnBadArgument;
        IOReturn ret = cmd((UInt64)SMC_READ_KEY | ((UInt64)len << SMC_MSG_SIZE_SHIFT) | ((UInt64)key << SMC_MSG_KEY_SHIFT),
                           "the SMC read-key reply", &r);
        if (res)
            *res = r;
        if (ret != kIOReturnSuccess)
            return ret;
        if (len <= 4) {
            UInt32 value = OSSwapHostToLittleInt32(SMC_RESULT_VALUE(result));
            memcpy(buf, &value, len);
        } else {
            UInt32 got = SMC_RESULT_SIZE(result);
            if (got > len)
                got = len;
            memset(buf, 0, len);
            shmRead((UInt8 *)buf, got);
        }
        return kIOReturnSuccess;
    }

    /* m1n1 smc_write_u32(), Python SMCEndpoint.write() */
    IOReturn writeKey(UInt32 key, const void *buf, UInt32 len, UInt32 *res)
    {
        if (!shm.va || !len || len > 255)
            return kIOReturnBadArgument;
        const UInt8 *src = (const UInt8 *)buf;
        for (UInt32 i = 0; i < len; i++)
            shm.va[i] = src[i];
        OSSynchronizeIO();
        return cmd((UInt64)SMC_WRITE_KEY | ((UInt64)len << SMC_MSG_SIZE_SHIFT) | ((UInt64)key << SMC_MSG_KEY_SHIFT),
                   "the SMC write-key reply", res);
    }

    /* Python SMCEndpoint.get_key_info(): length, type FourCC, flags */
    IOReturn keyInfo(UInt32 key, UInt8 info[6], UInt32 *res)
    {
        if (!shm.va)
            return kIOReturnBadArgument;
        IOReturn ret = cmd((UInt64)SMC_GET_KEY_INFO | ((UInt64)key << SMC_MSG_KEY_SHIFT), "the SMC key-info reply", res);
        if (ret == kIOReturnSuccess)
            shmRead(info, 6);
        return ret;
    }

    /*
     * rtkit_quiesce() / Python mgmt.stop(): AP power 0x10, then IOP power 0x10. The CPU
     * is left started (m1n1 stops it only for the sleep state).
     */
    void close()
    {
        if (!mbox.va || !cpu.va)
            return;
        if (!talked || !helloSeen || failed) {
            LOG("no quiesce (%s); the SMC is left as it is",
                failed ? "session failed" : helloSeen ? "nothing sent" : "no HELLO was seen");
            logMailbox("mailbox state after");
            return;
        }
        LOG("quiescing: AP power state 0x%x", RTKIT_POWER_QUIESCED);
        bool ok = send(RTKIT_EP_MGMT, ((UInt64)MGMT_MSG_AP_PWR_STATE << MGMT_TYPE_SHIFT) | RTKIT_POWER_QUIESCED) &&
                  waitFor(kCondApQuiesced, WAIT_QUIESCE_US, "AP power state 0x10");
        if (ok) {
            LOG("quiescing: IOP power state 0x%x", RTKIT_POWER_QUIESCED);
            ok = send(RTKIT_EP_MGMT, ((UInt64)MGMT_MSG_IOP_PWR_STATE << MGMT_TYPE_SHIFT) | RTKIT_POWER_QUIESCED) &&
                 waitFor(kCondIopQuiesced, WAIT_QUIESCE_US, "IOP power state 0x10");
        }
        if (!ok)
            LOG("quiesce did NOT complete; the SMC may be left in RTKit power state iop 0x%x ap 0x%x", iopPower,
                apPower);
        logMailbox("mailbox state after");
    }
};

bool sessionEnter()
{
    if (!OSCompareAndSwap(0, 1, &gBusy)) {
        LOG("another SMC session is running; refused");
        return false;
    }
    return true;
}

void sessionLeave()
{
    OSCompareAndSwap(1, 0, &gBusy);
}

IOReturn radioPower(UInt32 options, bool on, bool *wrote)
{
    const UInt32 key = SF_SMC_KEY_RADIO_POWER;
    const UInt32 want = on ? SF_SMC_RADIO_POWER_ON : SF_SMC_RADIO_POWER_OFF;

    if (wrote)
        *wrote = false;
    if (!sessionEnter())
        return kIOReturnBusy;

    IOReturn ret;
    bool written = false;
    {
        Smc smc;
        ret = smc.open(options);
        if (ret == kIOReturnSuccess) {
            UInt8 info[6] = {0};
            UInt32 res = 0, value = 0;
            bool skip = false;

            LOG("first access to the SMC shared memory at 0x%llx comes with the next replies",
                (unsigned long long)smc.shmem);
            IOReturn r = smc.keyInfo(key, info, &res);
            if (r == kIOReturnSuccess)
                LOG("gP0d key info: length %u type '%c%c%c%c' flags 0x%02x", info[0], info[1], info[2], info[3],
                    info[4], info[5]);
            else
                LOG("gP0d key info failed (0x%08x, SMC result 0x%02x); going on", r, res);

            res = 0;
            r = smc.readKey(key, &value, sizeof(value), &res);
            if (r == kIOReturnSuccess) {
                value = OSSwapLittleToHostInt32(value);
                bool isOn = (value & 1) != 0;
                LOG("gP0d reads 0x%08x (%s)", value, isOn ? "bit 0 set: enabled" : "bit 0 clear: disabled");
                if (isOn == on && !(options & kSFAppleSMCAlwaysWrite)) {
                    LOG("radio power is already %s; gP0d NOT written (sfapciesmc bit 1 forces the write)",
                        on ? "enabled" : "disabled");
                    skip = true;
                }
            } else {
                LOG("gP0d read failed (0x%08x, SMC result 0x%02x); writing anyway", r, res);
            }

            if (!skip && !smc.failed) {
                UInt32 le = OSSwapHostToLittleInt32(want);
                res = 0;
                ret = smc.writeKey(key, &le, sizeof(le), &res);
                if (ret == kIOReturnSuccess) {
                    LOG("gP0d = 0x%08x written, SMC result 0 (radio power %s)", want, on ? "on" : "off");
                    written = true;
                } else {
                    LOG("gP0d = 0x%08x write FAILED (0x%08x, SMC result 0x%02x)", want, ret, res);
                }
            } else if (smc.failed) {
                ret = kIOReturnIOError;
            }
        } else {
            LOG("SMC session could not be opened (0x%08x); gP0d not touched", ret);
        }
        smc.close();
    }
    sessionLeave();

    if (written) {
        LOG("waiting 100 ms after the radio power write");
        IOSleep(100);
    }
    if (wrote)
        *wrote = written;
    return ret;
}

IOReturn oneKey(UInt32 options, UInt32 key, void *rbuf, const void *wbuf, UInt32 len, UInt32 *smcResult)
{
    if (smcResult)
        *smcResult = 0;
    if (!sessionEnter())
        return kIOReturnBusy;
    IOReturn ret;
    {
        Smc smc;
        ret = smc.open(options);
        if (ret == kIOReturnSuccess)
            ret = wbuf ? smc.writeKey(key, wbuf, len, smcResult) : smc.readKey(key, rbuf, len, smcResult);
        smc.close();
    }
    sessionLeave();
    return ret;
}

} /* namespace */

IOReturn SFAppleSMCRadioPowerOn(UInt32 options, bool *wrote)
{
    return radioPower(options, true, wrote);
}

IOReturn SFAppleSMCRadioPowerOff(UInt32 options)
{
    return radioPower(options, false, NULL);
}

IOReturn SFAppleSMCReadKey(UInt32 options, UInt32 key, void *buf, UInt32 len, UInt32 *smcResult)
{
    return oneKey(options, key, buf, NULL, len, smcResult);
}

IOReturn SFAppleSMCWriteKey(UInt32 options, UInt32 key, const void *buf, UInt32 len, UInt32 *smcResult)
{
    if (!buf)
        return kIOReturnBadArgument;
    return oneKey(options, key, NULL, buf, len, smcResult);
}
