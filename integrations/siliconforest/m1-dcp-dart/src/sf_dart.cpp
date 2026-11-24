// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * sf_dart.cpp - streams of the display DARTs of T8103 (/arm-io/dart-dcp, /arm-io/dart-disp0;
 * T8020 DARTs) as firmware left them. IOKit side of sf_dart.h; the table code is
 * sf_dart_table.c.
 *
 * Grown out of ../m1-gpu-rtkit/probe/sf_dcp_dart.cpp, which booted on this M1 (dc11): the
 * registers read there, the walk of the live root, one first-level entry added in an empty
 * slot and leaves in a table of our own, each followed by a stream TLB invalidate.
 *
 * What is written, per stream:
 *   - first-level entries of the live root, only in slots that read 0, pointing at tables of
 *     this kernel's memory;
 *   - leaf entries, only where the slot reads 0: in our tables, and in a table that was
 *     already there when a caller maps at an explicit address inside its range (m1n1
 *     dart_get_l2 follows a valid entry the same way; Linux's piodma mappings land in
 *     whatever table covers the address the firmware chose);
 *   - STREAM_SELECT (0x34) and STREAM_COMMAND (0x20) of the DART, for the TLB invalidate.
 * Never written: TCR, TTBR, CONFIG, stream enables, the error register, any entry that
 * reads nonzero (so no existing mapping, the boot framebuffer's included, can change).
 *
 * References (the lab's pins): Linux drivers/iommu/apple-dart.c and io-pgtable-dart.c
 * (GPL-2.0-only, Copyright (C) 2021 The Asahi Linux Contributors), m1n1 src/dart.c (MIT,
 * Copyright The Asahi Linux Contributors).
 */
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <libkern/OSAtomic.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>
#include <sys/errno.h>

#include "sf_dart.h"
#include "sf_dart_table.h"
#include "sf_dart_retire.h"
#include "sf_dart_pair.h"
#include "sf_dart_spec.h"

/* apple-dart.c, T8020 */
#define DART_PARAMS1                   0x00
#define DART_PARAMS1_PAGE_SHIFT(v)     (((v) >> 24) & 0xf)
#define DART_STREAM_COMMAND            0x20
#define DART_STREAM_COMMAND_BUSY       (1u << 2)
#define DART_STREAM_COMMAND_INVALIDATE (1u << 20)
#define DART_STREAM_SELECT             0x34
#define DART_ERROR                     0x40
#define DART_ERROR_FLAG                (1u << 31)
#define DART_ERROR_ADDR_LO             0x50
#define DART_ERROR_ADDR_HI             0x54
#define DART_CONFIG                    0x60
#define DART_CONFIG_LOCK               (1u << 15)
#define DART_ENABLED_STREAMS           0xfc
#define DART_TCR(sid)                  (0x100 + 4 * (sid))
#define DART_TCR_TRANSLATE_ENABLE      (1u << 7)
#define DART_TCR_BYPASS_DART           (1u << 8)
#define DART_TCR_BYPASS_DAPF           (1u << 12)
#define DART_TTBR(sid, idx)            (0x200 + 16 * (sid) + 4 * (idx))
#define DART_TTBR_VALID                (1u << 31)
#define DART_TTBR_SHIFT                12

/*
 * Where allocations come from: first-level slots 8..71, iova 0x10000000..0x90000000. It
 * starts where m1n1's window does (dcp.c:210) and lies inside the streams' range on T8103
 * (vm-size / Linux's iommus window 0xfc000000). A slot that is not empty when the stream
 * is opened is left alone entirely.
 */
#define WINDOW_LO      SF_DART_WINDOW_LO
#define WINDOW_HI      SF_DART_WINDOW_HI
#define WINDOW_PAGES   ((WINDOW_HI - WINDOW_LO) >> SF_DART_PAGE_SHIFT)

#define MAX_DARTS      4
#define MAX_STREAMS    8
#define MAX_FOREIGN    24 /* table pages that were there before us, mapped by physical address */
#define MAX_OWNED      80 /* table pages of ours: one per first-level slot we fill */

namespace {

struct Dart {
    uint64_t regsPhys;
    char path[64];
    IOMemoryDescriptor *md;
    IOMemoryMap *map;
    volatile uint8_t *regs;
};

struct TablePage {
    uint64_t phys;
    uint64_t *va;
    OSObject *md;  /* IOBufferMemoryDescriptor (ours) or IOMemoryDescriptor (foreign) */
    IOMemoryMap *map; /* foreign only */
};

} // namespace

struct sf_dart_stream {
    bool used;
    Dart *dart;
    uint32_t sid;
    struct sf_dart_info info;
    struct sf_dart_tables tables;
    TablePage foreign[MAX_FOREIGN];
    TablePage owned[MAX_OWNED];
    uint8_t *busy; /* one bit per page of the window */
    uint32_t invalidates;
    bool poisoned; // failed invalidate: no new mappings or IOVA reclamation
};

namespace {

IOLock *gLock;
Dart gDarts[MAX_DARTS];
sf_dart_stream gStreams[MAX_STREAMS];
// PIODMA and RTKit/display streams can reach the same backing. Failure in
// any stream therefore closes allocation/retirement across this DCP copy.
bool anyPoisoned()
{
    for (unsigned i=0;i<MAX_STREAMS;++i)
        if (gStreams[i].used && gStreams[i].poisoned) return true;
    return false;
}
const char *gPrefix = "SFDcpDart: ";

void dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void dlog(const char *fmt, ...)
{
    char line[440];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    IOLog("%s%s\n", gPrefix, line);
}

inline uint32_t rd(Dart *d, uint32_t off)
{
    uint32_t v = OSReadLittleInt32((volatile void *)d->regs, off);
    OSSynchronizeIO();
    return v;
}

inline void wr(Dart *d, uint32_t off, uint32_t v)
{
    OSWriteLittleInt32((volatile void *)d->regs, off, v);
    OSSynchronizeIO();
}

const char *whose(uint64_t phys)
{
    boot_args *args = (boot_args *)PE_state.bootArgs;

    return phys >= args->physBase && phys - args->physBase < args->memSize ? "inside kernel memory" : "outside kernel memory";
}

/*
 * sf_dart_table_ops. A table of ours is reached through the descriptor that owns it (a
 * mapping by physical address of a page this kernel manages draws "IOMemoryDescriptor
 * physical with managed page", seen in boot dc11). Only tables that were there before us
 * are mapped by physical address, as the Linux layer's memremap() does for the GPU's.
 */
uint64_t *opTable(void *ctx, uint64_t phys)
{
    sf_dart_stream *s = (sf_dart_stream *)ctx;
    TablePage *free = NULL;

    for (int i = 0; i < MAX_OWNED; i++)
        if (s->owned[i].va && s->owned[i].phys == phys)
            return s->owned[i].va;
    for (int i = 0; i < MAX_FOREIGN; i++) {
        if (s->foreign[i].va && s->foreign[i].phys == phys)
            return s->foreign[i].va;
        if (!s->foreign[i].va && !free)
            free = &s->foreign[i];
    }
    if (!free || (phys & (SF_DART_PAGE - 1)))
        return NULL;
    IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress((IOPhysicalAddress)phys, SF_DART_PAGE,
                                                                     kIODirectionNone | kIOMemoryMapperNone);
    IOMemoryMap *map = md ? md->map(kIOMapAnywhere) : NULL;
    if (!map || !map->getVirtualAddress()) {
        OSSafeReleaseNULL(map);
        OSSafeReleaseNULL(md);
        return NULL;
    }
    free->phys = phys;
    free->md = md;
    free->map = map;
    free->va = (uint64_t *)map->getVirtualAddress();
    return free->va;
}

uint64_t opAllocTable(void *ctx)
{
    sf_dart_stream *s = (sf_dart_stream *)ctx;

    for (int i = 0; i < MAX_OWNED; i++) {
        if (s->owned[i].va)
            continue;
        /* wired, 16K aligned, below 36 bits; kept for as long as the system runs */
        IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous, SF_DART_PAGE, 0x0000000fffffc000ULL);
        if (!md || md->prepare() != kIOReturnSuccess) {
            OSSafeReleaseNULL(md);
            return 0;
        }
        bzero(md->getBytesNoCopy(), SF_DART_PAGE);
        uint64_t phys = md->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
        if (!phys || (phys & (SF_DART_PAGE - 1))) {
            md->complete();
            md->release();
            return 0;
        }
        s->owned[i].phys = phys;
        s->owned[i].md = md;
        s->owned[i].va = (uint64_t *)md->getBytesNoCopy();
        return phys;
    }
    return 0;
}

int invalidate(sf_dart_stream *s)
{
    /* the table writes must be in memory before the DART is told to look again */
    __asm__ volatile("dsb sy" ::: "memory");
    wr(s->dart, DART_STREAM_SELECT, 1u << s->sid);
    wr(s->dart, DART_STREAM_COMMAND, DART_STREAM_COMMAND_INVALIDATE);
    s->invalidates++;
    for (int i = 0; i < 1000; i++) {
        if (!(rd(s->dart, DART_STREAM_COMMAND) & DART_STREAM_COMMAND_BUSY))
            return 0;
        IODelay(100);
    }
    s->poisoned=true;
    dlog("dart %s stream %u: TLB invalidate BUSY did not clear in 100 ms", s->dart->path, (unsigned)s->sid);
    return -ETIMEDOUT;
}

Dart *dartFor(const char *path, uint64_t regsPhys)
{
    Dart *free = NULL;

    for (int i = 0; i < MAX_DARTS; i++) {
        if (gDarts[i].regs && ((path && !strcmp(gDarts[i].path, path)) || (!path && gDarts[i].regsPhys == regsPhys)))
            return &gDarts[i];
        if (!gDarts[i].regs && !free)
            free = &gDarts[i];
    }
    if (!free)
        return NULL;
    if (path) {
        IORegistryEntry *node = IORegistryEntry::fromPath(path, gIODTPlane);
        OSData *compat = node ? OSDynamicCast(OSData, node->getProperty("compatible")) : NULL;
        bool t8020 = compat && compat->getLength() >= sizeof("dart,t8020") &&
                     !memcmp(compat->getBytesNoCopy(), "dart,t8020", sizeof("dart,t8020"));
        /* registry-owned array: used here, never released; reached once per DART */
        OSArray *regs = t8020 ? IODTResolveAddressing(node, "reg", NULL) : NULL;
        IOMemoryDescriptor *reg = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
        regsPhys = reg ? reg->getPhysicalSegment(0, NULL, kIOMemoryMapperNone) : 0;
        OSSafeReleaseNULL(node);
        if (!regsPhys) {
            dlog("dart %s: not found in the device tree, not a dart,t8020, or reg 0 not resolved", path);
            return NULL;
        }
        /* the same DART may already be open by its register address */
        for (int i = 0; i < MAX_DARTS; i++)
            if (gDarts[i].regs && gDarts[i].regsPhys == regsPhys)
                return &gDarts[i];
    }
    if (!regsPhys || (regsPhys & (SF_DART_PAGE - 1)))
        return NULL;
    /* on-SoC registers: non-posted */
    free->md = IODeviceMemory::withRange((IOPhysicalAddress)regsPhys, SF_DART_PAGE);
    free->map = free->md ? free->md->map(kIOMapInhibitCache) : NULL;
    if (!free->map || !free->map->getVirtualAddress()) {
        OSSafeReleaseNULL(free->map);
        OSSafeReleaseNULL(free->md);
        dlog("dart at 0x%llx: registers could not be mapped", (unsigned long long)regsPhys);
        return NULL;
    }
    free->regsPhys = regsPhys;
    if (path)
        strlcpy(free->path, path, sizeof(free->path));
    else
        snprintf(free->path, sizeof(free->path), "dart@%llx", (unsigned long long)regsPhys);
    free->regs = (volatile uint8_t *)free->map->getVirtualAddress();
    return free;
}

inline void busySet(sf_dart_stream *s, uint64_t page, bool v)
{
    if (v)
        s->busy[page >> 3] |= (uint8_t)(1u << (page & 7));
    else
        s->busy[page >> 3] &= (uint8_t)~(1u << (page & 7));
}

} // namespace

extern "C" {

void sf_dart_set_log_prefix(const char *prefix)
{
    gPrefix = prefix ? prefix : "";
}

struct sf_dart_stream *sf_dart_open(const char *path, uint64_t regs_phys, uint32_t sid)
{
    if (!gLock) {
        /* two streams may be opened at once from different CPUs: one lock wins */
        IOLock *fresh = IOLockAlloc();
        if (fresh && !OSCompareAndSwapPtr(NULL, fresh, (void *volatile *)&gLock))
            IOLockFree(fresh);
    }
    if (!gLock || sid >= 16)
        return NULL;
    IOLockLock(gLock);
    sf_dart_stream *s = NULL, *made = NULL;
    do {
        Dart *d = dartFor(path, regs_phys);
        if (!d)
            break;
        for (int i = 0; i < MAX_STREAMS; i++) {
            if (gStreams[i].used && gStreams[i].dart == d && gStreams[i].sid == sid)
                s = &gStreams[i];
            else if (!gStreams[i].used && !made)
                made = &gStreams[i];
        }
        if (s || !made)
            break;

        struct sf_dart_info *in = &made->info;
        bzero(made, sizeof(*made));
        made->dart = d;
        made->sid = sid;
        strlcpy(in->path, d->path, sizeof(in->path));
        in->regs_phys = d->regsPhys;
        in->sid = sid;
        in->params1 = rd(d, DART_PARAMS1);
        in->config = rd(d, DART_CONFIG);
        in->tcr = rd(d, DART_TCR(sid));
        for (int i = 0; i < 4; i++)
            in->ttbr[i] = rd(d, DART_TTBR(sid, i));
        in->enabled_streams = rd(d, DART_ENABLED_STREAMS);
        in->error = rd(d, DART_ERROR);
        in->locked = !!(in->config & DART_CONFIG_LOCK);
        in->window_lo = WINDOW_LO;
        in->window_hi = WINDOW_HI;
        dlog("dart-regs: %s at 0x%llx stream %u: params1 0x%08x config 0x%08x (%s) tcr 0x%08x ttbr 0x%08x 0x%08x 0x%08x 0x%08x enabled-streams 0x%08x error 0x%08x",
             d->path, (unsigned long long)d->regsPhys, (unsigned)sid, in->params1, in->config,
             in->locked ? "locked" : "not locked", in->tcr, in->ttbr[0], in->ttbr[1], in->ttbr[2], in->ttbr[3],
             in->enabled_streams, in->error);

        const char *why = NULL;
        if (DART_PARAMS1_PAGE_SHIFT(in->params1) != SF_DART_PAGE_SHIFT)
            why = "page size is not 16K";
        else if (!(in->enabled_streams & (1u << sid)))
            why = "the stream is not enabled";
        /* m1n1 dart.c:481-486: translating, not bypassed */
        else if ((in->tcr & (DART_TCR_TRANSLATE_ENABLE | DART_TCR_BYPASS_DART | DART_TCR_BYPASS_DAPF)) !=
                 DART_TCR_TRANSLATE_ENABLE)
            why = "the stream is not translating";
        else if (!(in->ttbr[0] & DART_TTBR_VALID))
            why = "TTBR 0 is not valid";
        if (why) {
            dlog("dart-stop: %s stream %u: %s; the stream is not used", d->path, (unsigned)sid, why);
            break;
        }

        in->l1_phys = (uint64_t)(in->ttbr[0] & ~DART_TTBR_VALID) << DART_TTBR_SHIFT;
        made->tables.ops.ctx = made;
        made->tables.ops.table = &opTable;
        made->tables.ops.alloc_table = &opAllocTable;
        made->tables.l1 = opTable(made, in->l1_phys);
        made->busy = (uint8_t *)IOMalloc(WINDOW_PAGES / 8);
        if (!made->tables.l1 || !made->busy) {
            dlog("dart-stop: %s stream %u: root at 0x%llx could not be mapped, or no memory", d->path, (unsigned)sid,
                 (unsigned long long)in->l1_phys);
            break;
        }
        bzero(made->busy, WINDOW_PAGES / 8);

        char list[200];
        size_t n = 0;
        list[0] = '\0';
        unsigned windowUsed = 0;
        for (unsigned i = 0; i < SF_DART_PTES; i++) {
            uint64_t e = made->tables.l1[i];
            if (e != 0 && (uint64_t)i * SF_DART_L2_SPAN >= WINDOW_LO && (uint64_t)i * SF_DART_L2_SPAN < WINDOW_HI) {
                /* not empty: none of this slot is handed out */
                uint64_t first = ((uint64_t)i * SF_DART_L2_SPAN - WINDOW_LO) >> SF_DART_PAGE_SHIFT;
                for (uint64_t p = 0; p < SF_DART_PTES; p++)
                    busySet(made, first + p, true);
                windowUsed++;
            }
            if (!(e & SF_DART_PTE_VALID))
                continue;
            if (in->l1_valid < 7 && n + 28 < sizeof(list))
                n += snprintf(list + n, sizeof(list) - n, " [%u]=0x%llx", i, (unsigned long long)e);
            in->l1_valid++;
        }
        dlog("dart-l1: %s stream %u: root at phys 0x%llx (%s), %u of %u entries valid:%s%s; allocation window iova 0x%llx..0x%llx, %u of its %u slots already in use",
             d->path, (unsigned)sid, (unsigned long long)in->l1_phys, whose(in->l1_phys), (unsigned)in->l1_valid,
             SF_DART_PTES, list, in->l1_valid > 7 ? " ..." : "", WINDOW_LO, WINDOW_HI, windowUsed,
             (unsigned)((WINDOW_HI - WINDOW_LO) / SF_DART_L2_SPAN));
        made->used = true;
        s = made;
    } while (false);
    IOLockUnlock(gLock);
    return s;
}

void sf_dart_get_info(struct sf_dart_stream *s, struct sf_dart_info *info)
{
    if (s && info)
        *info = s->info;
}

uint64_t sf_dart_walk(struct sf_dart_stream *s, uint64_t iova, uint64_t *l1e, uint64_t *pte)
{
    struct sf_dart_walk w = {};

    if (!s)
        return 0;
    IOLockLock(gLock);
    sf_dart_table_walk(&s->tables, iova, &w);
    IOLockUnlock(gLock);
    if (l1e)
        *l1e = w.l1e;
    if (pte)
        *pte = w.pte;
    return w.phys;
}

uint64_t sf_dart_translate(struct sf_dart_stream *s, uint64_t iova)
{
    return sf_dart_walk(s, iova, NULL, NULL);
}

int sf_dart_find(struct sf_dart_stream *s, uint64_t phys, uint64_t size, uint64_t *iova)
{
    if (!s || !iova)
        return -EINVAL;
    IOLockLock(gLock);
    int ret = sf_dart_table_find(&s->tables, phys, size, 0, 1ULL << 32, iova);
    IOLockUnlock(gLock);
    return ret;
}

int sf_dart_map(struct sf_dart_stream *s, uint64_t iova, uint64_t phys, uint64_t size, unsigned int prot)
{
    if (!s)
        return -EINVAL;
    IOLockLock(gLock);
    if (anyPoisoned()) { IOLockUnlock(gLock); return -EIO; }
    int ret = sf_dart_table_map(&s->tables, iova, phys, size, prot);
    /* also after a failure that took entries back: the DART must not keep what it may have fetched */
    int inv = invalidate(s);
    IOLockUnlock(gLock);
    return inv ? inv : ret;
}

int sf_dart_map_deferred(struct sf_dart_stream *s, uint64_t iova, uint64_t phys, uint64_t size, unsigned int prot)
{
    if (!s)
        return -EINVAL;
    IOLockLock(gLock);
    if (anyPoisoned()) { IOLockUnlock(gLock); return -EIO; }
    int ret = sf_dart_table_map(&s->tables, iova, phys, size, prot);
    IOLockUnlock(gLock);
    return ret;
}

int sf_dart_flush(struct sf_dart_stream *s)
{
    if (!s)
        return -EINVAL;
    IOLockLock(gLock);
    int ret = anyPoisoned() ? -EIO : invalidate(s);
    IOLockUnlock(gLock);
    return ret;
}

static int retireInvalidate(void *ctx) { return invalidate((sf_dart_stream*)ctx); }
int sf_dart_unmap_checked(struct sf_dart_stream *s,uint64_t iova,uint64_t size,uint64_t *unmapped)
{
    if (!s || !unmapped) return -EINVAL;
    IOLockLock(gLock);
    int ret=anyPoisoned() ? -EIO : sf_dart_retire(&s->tables,&s->poisoned,iova,size,s,retireInvalidate,unmapped);
    IOLockUnlock(gLock);
    return ret;
}
uint64_t sf_dart_unmap(struct sf_dart_stream *s,uint64_t iova,uint64_t size)
{
    uint64_t unmapped=0;
    return sf_dart_unmap_checked(s,iova,size,&unmapped)?0:unmapped;
}
int sf_dart_poisoned(struct sf_dart_stream *s)
{
    if (!s) return 1;
    IOLockLock(gLock);bool value=anyPoisoned();IOLockUnlock(gLock);
    return value;
}

int sf_dart_iova_alloc_pair(struct sf_dart_stream *a,struct sf_dart_stream *b,uint64_t size,uint64_t *iova)
{
    if (!a || a==b || !iova || !size || size>WINDOW_HI-WINDOW_LO)return -EINVAL;
    uint64_t pages=(size+SF_DART_PAGE-1)>>SF_DART_PAGE_SHIFT,first=0;
    IOLockLock(gLock);
    if (anyPoisoned()) { IOLockUnlock(gLock); return -EIO; }
    int ret=sf_dart_pair_find(a->busy,b?b->busy:NULL,WINDOW_PAGES,pages,&first);
    if (!ret) {
        for (uint64_t q=first;q<first+pages;++q) {
            busySet(a,q,true);if(b)busySet(b,q,true);
        }
        *iova=WINDOW_LO+(first<<SF_DART_PAGE_SHIFT);
    }
    IOLockUnlock(gLock);return ret;
}
int sf_dart_iova_alloc(struct sf_dart_stream *s,uint64_t size,uint64_t *iova)
{ return sf_dart_iova_alloc_pair(s,NULL,size,iova); }
void sf_dart_iova_free_pair(struct sf_dart_stream *a,struct sf_dart_stream *b,uint64_t iova,uint64_t size)
{
    if (!a || a==b || !size || size>WINDOW_HI-WINDOW_LO || iova<WINDOW_LO ||
        iova>WINDOW_HI || (iova&(SF_DART_PAGE-1)))return;
    uint64_t pages=(size+SF_DART_PAGE-1)>>SF_DART_PAGE_SHIFT;
    if ((pages<<SF_DART_PAGE_SHIFT)>WINDOW_HI-iova)return;
    IOLockLock(gLock);
    if (anyPoisoned()) { IOLockUnlock(gLock); return; }
    uint64_t first=(iova-WINDOW_LO)>>SF_DART_PAGE_SHIFT;
    for (uint64_t q=first;q<first+pages;++q) {
        busySet(a,q,false);if(b)busySet(b,q,false);
    }
    IOLockUnlock(gLock);
}
void sf_dart_iova_free(struct sf_dart_stream *s,uint64_t iova,uint64_t size)
{ sf_dart_iova_free_pair(s,NULL,iova,size); }

uint32_t sf_dart_check(struct sf_dart_stream *s, const char *when)
{
    if (!s)
        return 0;
    uint32_t err = rd(s->dart, DART_ERROR);
    /* bit 31: a fault is recorded; 27:24: stream of the last one; 11:0: cause (apple-dart.c:77-86) */
    if (err & DART_ERROR_FLAG)
        dlog("dart-error: %s (%s): error 0x%08x stream %u code 0x%03x address 0x%08x%08x; not cleared", s->dart->path,
             when ? when : "", err, (err >> 24) & 0xf, err & 0xfff, rd(s->dart, DART_ERROR_ADDR_HI),
             rd(s->dart, DART_ERROR_ADDR_LO));
    return err;
}

void sf_dart_stats(struct sf_dart_stream *s, uint32_t *l1_writes, uint32_t *leaf_writes, uint32_t *leaf_clears,
                   uint32_t *invalidates)
{
    if (l1_writes)
        *l1_writes = s ? s->tables.l1_writes : 0;
    if (leaf_writes)
        *leaf_writes = s ? s->tables.leaf_writes : 0;
    if (leaf_clears)
        *leaf_clears = s ? s->tables.leaf_clears : 0;
    if (invalidates)
        *invalidates = s ? s->invalidates : 0;
}

} // extern "C"
