// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * SFRtkitPlatform.cpp - the IOKit side of sf_rtkit_platform.h: device-tree
 * lookup of an ASC mailbox, non-posted register mappings, AIC interrupts with
 * Linux enable/disable semantics, a read-only PMGR power-state check.
 *
 * Rules taken from code that already runs on this machine
 * (../m1-apcie-host/SFAppleSMC.cpp, SFApcieT8103.cpp):
 *   - on-SoC registers are mapped with kIOMapInhibitCache (non-posted);
 *   - a device-tree node is a bare IOService in gIODTPlane that nobody
 *     started, so interrupts are described on a service of our own
 *     (controller gIODTDefaultInterruptController, specifier = AIC number),
 *     as SFApcieT8103Host::setupMSI does for MSI vectors;
 *   - IODTResolveAddressing() returns an array the registry owns: it is used
 *     at once, never released, and asked for once per node.
 *
 * Nothing here is verified on hardware. See README.md, "Unverified".
 */
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOInterruptController.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>

#include "sf_rtkit_platform.h"
#include "sf_rtkit_layout.h"

#define SOC_PAGE 0x4000ULL

/* T8103: PMGR power-state register of the GFX domain (Linux ps_gfx: pmgr@23b700000 + 0x3f8) */
#define T8103_PMGR_BASE   0x23b700000ULL
#define T8103_PMGR_PS_GFX 0x3f8
#define PMGR_PS_ACTUAL(v) (((v) >> 4) & 0xf)
#define PMGR_PS_ACTIVE    0xf

/* sfrtkit_irq=<n>: how a mailbox interrupt line is taken */
enum {
    kIrqModePoll = 0,       /* no AIC interrupt: recv-not-empty is a 2 ms timer, TX polls */
    kIrqModeEnable = 1,     /* unmask with IOService::enableInterrupt only */
    kIrqModeReregister = 2, /* after a delivered interrupt, unregister + register + enable (default) */
};
#define POLL_PERIOD_MS 2

#define MAX_IRQS  16
#define MAX_MBOX  8
#define MAX_MAPS  16

namespace {

struct Irq {
    bool used;
    bool isRecv;             /* recv-not-empty (pollable) or send-empty */
    IOService *nub;          /* our service carrying the 4 interrupt specifiers */
    int source;              /* index on the nub */
    UInt32 vector;           /* AIC number */
    sf_rtkit_irq_fn fn;
    void *arg;
    IOInterruptEventSource *soft; /* handler runs here, on gLoop's thread */
    IOTimerEventSource *timer;    /* poll mode */
    bool registered;         /* with the interrupt controller */
    volatile bool delivered; /* an interrupt was delivered since the last arm */
    int depth;               /* Linux disable depth: 0 = enabled */
    volatile SInt64 taken;
    UInt64 handled;
    char name[40];
};

struct Mbox {
    bool used;
    struct sf_rtkit_mbox_res res;
};

struct Mapping {
    void *va;
    IOMemoryDescriptor *md;
    IOMemoryMap *map;
};

IOService *gHost;
int gInitState; // 0 uninitialized, 1 allocating, 2 release-published
IOWorkLoop *gLoop;
IOLock *gLock; /* gIrqs[].depth/registered/fn, gMbox, gMaps; thread context only */
const char *gPrefix = "sf_rtkit: ";
int gIrqMode = kIrqModeReregister;
Irq gIrqs[MAX_IRQS];
Mbox gMbox[MAX_MBOX];
Mapping gMaps[MAX_MAPS];

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char *fmt, ...)
{
    char line[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sf_rtkit_platform_log(line);
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

/* "/arm-io/gfx-asc" stays, "gfx-asc" becomes "/arm-io/gfx-asc" */
bool fullPath(const char *path, char *out, size_t len)
{
    int n = path[0] == '/' ? snprintf(out, len, "%s", path) : snprintf(out, len, "/arm-io/%s", path);
    return n > 0 && (size_t)n < len;
}

Irq *irqFor(int irq)
{
    return irq >= 0 && irq < MAX_IRQS && gIrqs[irq].used ? &gIrqs[irq] : NULL;
}

/*
 * Primary interrupt level. The line is level triggered: mask it and hand over
 * to the thread. IOInterruptEventSource does the same for a level interrupt
 * (disableInterruptOccurred), but would unmask by itself after the action,
 * which is wrong once the Linux side has called disable_irq_nosync() from the
 * handler, as the mailbox does for send-empty.
 */
void primaryInterrupt(OSObject *target, void *refCon, IOService *nub, int source)
{
    Irq *q = (Irq *)refCon;

    nub->disableInterrupt(source);
    q->delivered = true;
    OSIncrementAtomic64(&q->taken);
    q->soft->interruptOccurred(NULL, NULL, 0);
}

/* gLock held. Unmasks at the controller, according to gIrqMode. */
void armLocked(Irq *q)
{
    if (!q->registered || gIrqMode == kIrqModePoll)
        return;
    if (q->delivered && gIrqMode == kIrqModeReregister) {
        /*
         * SFAppleAIC::handleInterrupt reads the AIC event register, which acks
         * and masks the line in hardware, and calls the handler without
         * recording that mask (interruptDisabledHard stays 0), so
         * IOInterruptController::enableInterrupt() would clear only the soft
         * flag and never write the unmask. A fresh registration starts
         * hard-disabled, and enabling that does write it.
         */
        q->nub->unregisterInterrupt(q->source);
        q->delivered = false;
        if (q->nub->registerInterrupt(q->source, q->nub, &primaryInterrupt, q) != kIOReturnSuccess) {
            q->registered = false;
            logf("irq %s: AIC %u could not be registered again", q->name, (unsigned)q->vector);
            return;
        }
    }
    q->delivered = false;
    q->nub->enableInterrupt(q->source);
}

void runHandler(Irq *q)
{
    sf_rtkit_irq_fn fn = NULL;
    void *arg = NULL;

    IOLockLock(gLock);
    if (q->used && q->depth == 0) {
        fn = q->fn;
        arg = q->arg;
    }
    IOLockUnlock(gLock);
    if (fn) {
        q->handled++;
        fn(arg);
    }
}

/* gLoop's thread, gate closed: no two handlers at once. */
void softAction(OSObject *owner, IOInterruptEventSource *sender, int count)
{
    for (int i = 0; i < MAX_IRQS; i++) {
        Irq *q = &gIrqs[i];
        if (!q->used || q->soft != sender)
            continue;
        runHandler(q);
        /* still enabled (the handler may have disabled it): unmask */
        IOLockLock(gLock);
        if (q->used && q->depth == 0)
            armLocked(q);
        IOLockUnlock(gLock);
        return;
    }
}

void pollAction(OSObject *owner, IOTimerEventSource *sender)
{
    for (int i = 0; i < MAX_IRQS; i++) {
        Irq *q = &gIrqs[i];
        if (!q->used || q->timer != sender)
            continue;
        runHandler(q);
        sender->setTimeoutMS(POLL_PERIOD_MS);
        return;
    }
}

IOReturn nopAction(OSObject *owner, void *a0, void *a1, void *a2, void *a3)
{
    return kIOReturnSuccess;
}

/* Returns once no handler is running, unless we are the handler thread. */
void drainHandlers()
{
    if (gLoop && !gLoop->inGate())
        gLoop->runAction(&nopAction, gHost);
}

int allocIrq(IOService *nub, int source, UInt32 vector, bool isRecv, const char *mboxName)
{
    for (int i = 0; i < MAX_IRQS; i++) {
        Irq *q = &gIrqs[i];
        if (q->used)
            continue;
        bzero(q, sizeof(*q));
        q->used = true;
        q->isRecv = isRecv;
        q->nub = nub;
        q->source = source;
        q->vector = vector;
        q->depth = 1; /* IRQF_NO_AUTOEN */
        snprintf(q->name, sizeof(q->name), "%s-%s", mboxName, isRecv ? "recv" : "send");
        return i;
    }
    return -1;
}

/*
 * A service of our own with the node's four AIC interrupts as its interrupt
 * sources 0..3, in the order the node lists them.
 */
IOService *makeInterruptNub(const char *name, const UInt32 *vectors)
{
    IOService *nub = new IOService;
    const OSSymbol *controllers[4];
    OSData *specs[4] = {NULL, NULL, NULL, NULL};
    OSArray *ca = NULL, *sa = NULL;
    bool ok = nub && nub->init();

    for (int i = 0; ok && i < 4; i++) {
        IOInterruptVectorNumber v = vectors[i];
        controllers[i] = gIODTDefaultInterruptController;
        specs[i] = OSData::withBytes(&v, sizeof(v));
        ok = specs[i] != NULL;
    }
    if (ok) {
        ca = OSArray::withObjects((const OSObject **)controllers, 4);
        sa = OSArray::withObjects((const OSObject **)specs, 4);
        ok = ca && sa && nub->setProperty(gIOInterruptControllersKey, ca) &&
             nub->setProperty(gIOInterruptSpecifiersKey, sa);
    }
    for (int i = 0; i < 4; i++)
        OSSafeReleaseNULL(specs[i]);
    OSSafeReleaseNULL(ca);
    OSSafeReleaseNULL(sa);
    if (ok) {
        char nubName[64];
        snprintf(nubName, sizeof(nubName), "SFRtkitMailbox-%s", name);
        nub->setName(nubName);
        ok = nub->attach(gHost);
    }
    if (!ok) {
        OSSafeReleaseNULL(nub);
        return NULL;
    }
    return nub;
}

} // namespace

extern "C" {

int sf_rtkit_platform_init(void *host)
{
    if (!host)
        return -EINVAL;
    for (;;) {
        if (__atomic_load_n(&gInitState,__ATOMIC_ACQUIRE)==2)
            return 0;
        int expected=0;
        if (__atomic_compare_exchange_n(&gInitState,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
            break;
        IOSleep(1);
    }
    IOLock *lock=IOLockAlloc();
    IOWorkLoop *loop=IOWorkLoop::workLoop();
    if (!lock || !loop) {
        if (lock) IOLockFree(lock);
        OSSafeReleaseNULL(loop);
        __atomic_store_n(&gInitState,0,__ATOMIC_RELEASE);
        return -ENOMEM;
    }
    gLock=lock;gLoop=loop;
    gHost=(IOService*)host;
    // IRQ/timer nubs use this anchor for the lifetime of this port's copy.
    // Per-mailbox clients retain their own native teardown responsibilities.
    gHost->retain();
    gIrqMode = sf_rtkit_platform_bootarg("sfrtkit_irq", kIrqModeReregister);
    if (gIrqMode < kIrqModePoll || gIrqMode > kIrqModeReregister)
        gIrqMode = kIrqModeReregister;
    __atomic_store_n(&gInitState,2,__ATOMIC_RELEASE);
    return 0;
}

void sf_rtkit_platform_set_log_prefix(const char *prefix)
{
    gPrefix = prefix ? prefix : "";
}

void sf_rtkit_platform_log(const char *line)
{
    IOLog("%s%s\n", gPrefix, line);
}

int sf_rtkit_platform_bootarg(const char *name, int def)
{
    int v = def;

    if (!PE_parse_boot_argn(name, &v, sizeof(v)))
        return def;
    return v;
}

int sf_rtkit_platform_mbox_resolve(const char *path, struct sf_rtkit_mbox_res *res)
{
    char full[SF_RTKIT_PATH_MAX];
    Mbox *m = NULL;
    int ret = 0;

    if (__atomic_load_n(&gInitState,__ATOMIC_ACQUIRE)!=2 || !path || !res || !fullPath(path, full, sizeof(full)))
        return -EINVAL;

    IOLockLock(gLock);
    for (int i = 0; i < MAX_MBOX; i++) {
        if (gMbox[i].used && !strcmp(gMbox[i].res.path, full)) {
            *res = gMbox[i].res;
            IOLockUnlock(gLock);
            return 0;
        }
        if (!gMbox[i].used && !m)
            m = &gMbox[i];
    }
    if (!m) {
        IOLockUnlock(gLock);
        return -ENOSPC;
    }

    IORegistryEntry *node = IORegistryEntry::fromPath(full, gIODTPlane);
    IOService *nub = NULL;
    do {
        if (!node) {
            logf("%s: no such node in the device tree", full);
            ret = -ENOENT;
            break;
        }
        /* iop,ascwrap-v8 and later are a different mailbox (Linux apple,asc-mailbox-v4 does not cover them) */
        bool m3=nodeIsCompatible(node,"iop,m3wrap-v2-acio");
        // Port0 is the reserved bench path. Only ACIO1 is qualified here.
        if (m3 && strcmp(full,"/arm-io/acio-cpu1")) {
            ret=-ENODEV;
            break;
        }
        if (!m3 && !nodeIsCompatible(node, "iop,ascwrap-v4")) {
            logf("%s: not an iop,ascwrap-v4 node", full);
            ret = -ENODEV;
            break;
        }

        bzero(&m->res, sizeof(m->res));
        strlcpy(m->res.path, full, sizeof(m->res.path));
        strlcpy(m->res.name, node->getName(gIODTPlane) ? node->getName(gIODTPlane) : "asc", sizeof(m->res.name));

        /* registry-owned array: used here, never released, asked for once per node */
        OSArray *regs = IODTResolveAddressing(node, "reg", NULL);
        IOMemoryDescriptor *d = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
        if (d) {
            m->res.asc_phys = d->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
            m->res.asc_size = d->getLength();
        }
        OSData *ints = OSDynamicCast(OSData, node->getProperty("interrupts"));
        UInt32 listed[4];
        if (!d || !ints || ints->getLength()!=sizeof(listed)) {
            ret=-EINVAL;
            break;
        }
        memcpy(listed,ints->getBytesNoCopy(),sizeof(listed));
        ret=sf_rtkit_mbox_layout(m3,m->res.asc_phys,m->res.asc_size,listed,&m->res);
        if (ret) {
            logf("%s: invalid %s mailbox registers or IRQ layout",full,m3?"M3v2":"ASCv4");
            break;
        }

        int srcSend = -1, srcRecv = -1;
        for (int i = 0; i < 4; i++) {
            if (m->res.aic_listed[i] == m->res.aic_send_empty)
                srcSend = i;
            if (m->res.aic_listed[i] == m->res.aic_recv_not_empty)
                srcRecv = i;
        }
        nub = makeInterruptNub(m->res.name, m->res.aic_listed);
        if (!nub) {
            ret = -ENOMEM;
            break;
        }
        m->res.irq_recv_not_empty = allocIrq(nub, srcRecv, m->res.aic_recv_not_empty, true, m->res.name);
        m->res.irq_send_empty =
            gIrqMode == kIrqModePoll ? -1 : allocIrq(nub, srcSend, m->res.aic_send_empty, false, m->res.name);
        if (m->res.irq_recv_not_empty < 0 || (gIrqMode != kIrqModePoll && m->res.irq_send_empty < 0)) {
            if (m->res.irq_recv_not_empty >= 0)
                gIrqs[m->res.irq_recv_not_empty].used = false;
            ret = -ENOSPC;
            break;
        }
        m->res.node = node; /* keeps the reference fromPath() gave */
        m->used = true;
        *res = m->res;
    } while (false);

    if (ret) {
        if (nub) {
            nub->detach(gHost);
            nub->release();
        }
        OSSafeReleaseNULL(node);
    }
    IOLockUnlock(gLock);
    return ret;
}

int sf_rtkit_platform_mbox_for_consumer(void *consumer_node, int index, char *path, size_t len)
{
    IORegistryEntry *node = (IORegistryEntry *)consumer_node;
    const char *name = node ? node->getName(gIODTPlane) : NULL;

    if (!path || index != 0)
        return -ENOENT;
    if (node && (nodeIsCompatible(node, "iop,ascwrap-v4") ||
                 nodeIsCompatible(node,"iop,m3wrap-v2-acio")) && name) {
        /* an ASC wrapper is its own mailbox; every one on this SoC is a child of /arm-io */
        return snprintf(path, len, "/arm-io/%s", name) < (int)len ? 0 : -EINVAL;
    }
    if (!node || (name && !strcmp(name, "sgx"))) {
        /* the GPU (Linux gpu@206400000: mboxes = <&agx_mbox>, mbox@206408000 = gfx-asc + 0x8000) */
        if (!node)
            logf("mailbox lookup for a consumer without a device-tree node: using /arm-io/gfx-asc");
        return strlcpy(path, "/arm-io/gfx-asc", len) < len ? 0 : -EINVAL;
    }
    logf("no mailbox known for consumer node %s", name ? name : "?");
    return -ENODEV;
}

void *sf_rtkit_platform_ioremap(uint64_t phys, uint64_t size)
{
    UInt64 start = phys & ~(SOC_PAGE - 1);
    UInt64 span = ((phys + size + SOC_PAGE - 1) & ~(SOC_PAGE - 1)) - start;
    IOMemoryDescriptor *md = IODeviceMemory::withRange((IOPhysicalAddress)start, (IOPhysicalLength)span);
    /* kIOMapInhibitCache: device memory, non-posted writes */
    IOMemoryMap *map = md ? md->map(kIOMapInhibitCache) : NULL;
    void *va = NULL;

    if (map && map->getVirtualAddress()) {
        IOLockLock(gLock);
        for (int i = 0; i < MAX_MAPS; i++) {
            if (!gMaps[i].va) {
                va = (UInt8 *)map->getVirtualAddress() + (phys - start);
                gMaps[i].va = va;
                gMaps[i].md = md;
                gMaps[i].map = map;
                break;
            }
        }
        IOLockUnlock(gLock);
    }
    if (!va) {
        logf("could not map 0x%llx len 0x%llx", (unsigned long long)phys, (unsigned long long)size);
        OSSafeReleaseNULL(map);
        OSSafeReleaseNULL(md);
    }
    return va;
}

void sf_rtkit_platform_iounmap(void *va)
{
    IOMemoryDescriptor *md = NULL;
    IOMemoryMap *map = NULL;

    if (!va)
        return;
    IOLockLock(gLock);
    for (int i = 0; i < MAX_MAPS; i++) {
        if (gMaps[i].va == va) {
            md = gMaps[i].md;
            map = gMaps[i].map;
            bzero(&gMaps[i], sizeof(gMaps[i]));
            break;
        }
    }
    IOLockUnlock(gLock);
    OSSafeReleaseNULL(map);
    OSSafeReleaseNULL(md);
}

int sf_rtkit_platform_irq_request(int irq, sf_rtkit_irq_fn fn, void *arg, const char *name)
{
    Irq *q = irqFor(irq);
    int ret = 0;

    if (!q || !fn)
        return -EINVAL;
    IOLockLock(gLock);
    do {
        if (q->fn) {
            ret = -EBUSY;
            break;
        }
        if (gIrqMode == kIrqModePoll) {
            if (!q->isRecv) {
                ret = -EINVAL;
                break;
            }
            q->timer = IOTimerEventSource::timerEventSource(gHost, &pollAction);
            if (!q->timer || gLoop->addEventSource(q->timer) != kIOReturnSuccess) {
                OSSafeReleaseNULL(q->timer);
                ret = -ENOMEM;
                break;
            }
            q->fn = fn;
            q->arg = arg;
            q->timer->setTimeoutMS(POLL_PERIOD_MS);
            logf("irq %s: no interrupt (sfrtkit_irq=0), handler polled every %d ms", q->name, POLL_PERIOD_MS);
            break;
        }
        /* a software interrupt source: primaryInterrupt() signals it, the action runs on gLoop */
        q->soft = IOInterruptEventSource::interruptEventSource(gHost, &softAction, NULL, 0);
        if (!q->soft || gLoop->addEventSource(q->soft) != kIOReturnSuccess) {
            OSSafeReleaseNULL(q->soft);
            ret = -ENOMEM;
            break;
        }
        q->soft->enable();
        q->fn = fn;
        q->arg = arg;
        /* registered interrupts start disabled at the controller: masked until irq_enable() */
        IOReturn kr = q->nub->registerInterrupt(q->source, q->nub, &primaryInterrupt, q);
        if (kr != kIOReturnSuccess) {
            logf("irq %s: AIC %u (source %d) could not be registered: 0x%x", q->name, (unsigned)q->vector, q->source,
                 kr);
            gLoop->removeEventSource(q->soft);
            OSSafeReleaseNULL(q->soft);
            q->fn = NULL;
            ret = -ENXIO;
            break;
        }
        q->registered = true;
        q->delivered = false;
        logf("irq %s: AIC %u registered, masked (mode %d)", q->name, (unsigned)q->vector, gIrqMode);
    } while (false);
    IOLockUnlock(gLock);
    return ret;
}

void sf_rtkit_platform_irq_free(int irq)
{
    Irq *q = irqFor(irq);

    if (!q)
        return;
    IOLockLock(gLock);
    q->depth++;
    q->fn = NULL;
    if (q->registered) {
        q->nub->disableInterrupt(q->source);
        q->nub->unregisterInterrupt(q->source);
        q->registered = false;
    }
    if (q->timer)
        q->timer->cancelTimeout();
    IOLockUnlock(gLock);
    drainHandlers();
    IOLockLock(gLock);
    if (q->soft) {
        gLoop->removeEventSource(q->soft);
        OSSafeReleaseNULL(q->soft);
    }
    if (q->timer) {
        gLoop->removeEventSource(q->timer);
        OSSafeReleaseNULL(q->timer);
    }
    /* the handle stays allocated to its mailbox: a later request may use it again */
    q->depth = 1;
    IOLockUnlock(gLock);
}

void sf_rtkit_platform_irq_enable(int irq)
{
    Irq *q = irqFor(irq);

    if (!q)
        return;
    IOLockLock(gLock);
    if (q->depth > 0 && --q->depth == 0)
        armLocked(q);
    IOLockUnlock(gLock);
}

void sf_rtkit_platform_irq_disable_nosync(int irq)
{
    Irq *q = irqFor(irq);

    if (!q)
        return;
    IOLockLock(gLock);
    if (q->depth++ == 0 && q->registered)
        q->nub->disableInterrupt(q->source);
    IOLockUnlock(gLock);
}

void sf_rtkit_platform_irq_disable(int irq)
{
    sf_rtkit_platform_irq_disable_nosync(irq);
    drainHandlers();
}

void sf_rtkit_platform_irq_synchronize(int irq)
{
    drainHandlers();
}

void sf_rtkit_platform_irq_stats(int irq, uint64_t *taken, uint64_t *handled)
{
    Irq *q = irqFor(irq);

    if (taken)
        *taken = q ? (uint64_t)q->taken : 0;
    if (handled)
        *handled = q ? q->handled : 0;
}

int sf_rtkit_platform_power_state(const char *path, uint32_t *raw)
{
    char full[SF_RTKIT_PATH_MAX];
    int ret = -ENODEV;

    if (!path || !raw || !fullPath(path, full, sizeof(full)))
        return -EINVAL;
    /* only the two nodes of the GFX domain, and only on the SoC whose PMGR layout is known here */
    if (strcmp(full, "/arm-io/gfx-asc") && strcmp(full, "/arm-io/sgx"))
        return -ENODEV;

    IORegistryEntry *armio = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
    IORegistryEntry *pmgr = IORegistryEntry::fromPath("/arm-io/pmgr", gIODTPlane);
    do {
        if (!armio || !pmgr || !nodeIsCompatible(armio, "arm-io,t8103"))
            break;
        static UInt64 pmgrBase; /* resolved once: registry-owned array */
        if (!pmgrBase) {
            OSArray *regs = IODTResolveAddressing(pmgr, "reg", NULL);
            IOMemoryDescriptor *d = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
            pmgrBase = d ? d->getPhysicalSegment(0, NULL, kIOMemoryMapperNone) : 0;
        }
        if (pmgrBase != T8103_PMGR_BASE) {
            logf("pmgr reg 0 is 0x%llx, expected 0x%llx: power state not read", (unsigned long long)pmgrBase,
                 (unsigned long long)T8103_PMGR_BASE);
            break;
        }
        volatile UInt32 *ps = (volatile UInt32 *)sf_rtkit_platform_ioremap(T8103_PMGR_BASE + T8103_PMGR_PS_GFX, 4);
        if (!ps) {
            ret = -ENOMEM;
            break;
        }
        *raw = *ps;
        OSSynchronizeIO();
        sf_rtkit_platform_iounmap((void *)ps);
        ret = PMGR_PS_ACTUAL(*raw) == PMGR_PS_ACTIVE ? 1 : 0;
    } while (false);
    OSSafeReleaseNULL(armio);
    OSSafeReleaseNULL(pmgr);
    return ret;
}

} // extern "C"
