// SPDX-License-Identifier: GPL-2.0
/*
 * sf_dcp_dart.cpp - the display coprocessor's DART for SFRtkitProbe's DCP mode (SF-H H2).
 *
 * /arm-io/dart-dcp on T8103 is a T8020 DART whose configuration iBoot locked: the stream's
 * TCR and TTBR registers cannot be changed, the page tables behind them are ordinary RAM.
 * The DCP firmware itself runs through stream 0 of this DART, so its own text, data and the
 * buffers it places are mapped there.
 *
 * How the two references treat it:
 *
 *   m1n1 (sources/m1n1-baseline, src/dart.c; this is what ran on this machine in every boot
 *   so far, before the kernel started):
 *     dart_init()                :341-384  locked: reads the roots from the TTBRs, writes no
 *                                          TTBR, TCR or stream enable
 *     dart_init_adt_display()    :469-496  T8103 dart-dcp only: requires the stream enabled
 *                                          and translating, then treats the tables as
 *                                          writable ("legacy_mutable")
 *     dart_get_l2()              :726-760  a valid first-level entry is followed; an empty
 *                                          one gets a new second-level table
 *     dart_map_page()            :762-783  refuses a slot that already has a valid entry
 *     dart_t8020_tlb_invalidate():136-146  STREAM_SELECT, STREAM_COMMAND, poll BUSY
 *     dcp.c:210                            AP allocations come from vm-base + 0x10000000 ..
 *                                          + 0x20000000; rtkit.c:174,343 hands the firmware
 *                                          iova | asc-dram-mask
 *
 *   Linux at the pin (sources/gpu-plan/linux-aurora, drivers/iommu/apple-dart.c):
 *     apple_dart_is_locked()          :2913  CONFIG bit 15
 *     probe                           :3197-3208  a locked DART is not reset; T8103 gets
 *                                          neither fw_handoff nor fw_mirror
 *     apple_dart_hw_map_locked_ttbr() :879-938  maps the root named by the TTBR (memremap),
 *                                          never writes the TTBR
 *     apple_dart_hw_sync_locked()     :1053-1070  "Legacy display DARTs hand their locked
 *                                          root to Linux": every entry of the live root is
 *                                          replaced by Linux's own, whose tables hold the
 *                                          firmware reservations m1n1 listed in the FDT
 *     apple_dart_iova_to_phys()       :1728-1746  firmware-placed addresses are masked to the
 *                                          table's input size and looked up in those tables
 *     io-pgtable-dart.c:122-135            leaf entry: address, subpage 0..0xfff, valid
 *
 * This file follows m1n1, the less invasive of the two: no existing entry is ever changed.
 * It differs from m1n1 in one respect: m1n1 reuses the second-level table it finds in the
 * first slot of its window (after m1n1's own session that table lives in m1n1's heap);
 * here allocations go to the first EMPTY first-level slot of that window, with a
 * second-level table of this kernel's own memory.
 *
 * Everything this file writes:
 *   - one 8-byte first-level entry in the live root, in a slot that read 0, pointing at
 *     our second-level table (once);
 *   - leaf entries in that table of ours;
 *   - STREAM_SELECT (0x34) and STREAM_COMMAND (0x20) of dart-dcp, for the TLB invalidate.
 * Never written: TCR, TTBR, CONFIG, the error register, any entry that was valid, anything
 * of dart-disp0.
 *
 * Register and table layout: Linux apple-dart.c / io-pgtable-dart.c (GPL-2.0-only,
 * Copyright (C) 2021 The Asahi Linux Contributors) and m1n1 src/dart.c (MIT, Copyright The
 * Asahi Linux Contributors), as in ../../m1-apcie-host/dart/sf_dart_pool.cpp.
 */
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>
#include <sys/errno.h>

#include "sf_dcp_dart.h"
#include "sf_rtkit_platform.h"

#define DART_NODE   "/arm-io/dart-dcp"
#define MAPPER_NODE "/arm-io/dart-dcp/mapper-dcp"
#define NUB_NODE    "/arm-io/dcp/iop-dcp-nub"

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

/* io-pgtable-dart.c, DART1 format */
#define PTE_PADDR_MASK  0x0000000ffffff000ULL
#define PTE_SP_END      (0xfffULL << 40) /* subpage end 0xfff, start 0: the whole page */
#define PTE_SP_DIS      (1ULL << 1)
#define PTE_VALID       (1ULL << 0)

#define PAGE_SHIFT_16K  14
#define PAGE_16K        (1ULL << PAGE_SHIFT_16K)
#define PTES            2048
#define L2_SPAN         ((uint64_t)PTES << PAGE_SHIFT_16K) /* 32 MiB per first-level entry */

/* m1n1 dcp.c:210: the window AP allocations come from, relative to vm-base (0 here) */
#define WINDOW_START    0x10000000ULL
#define WINDOW_END      0x20000000ULL
/* the stream's input size: firmware addresses are masked to this before the walk */
#define IOVA_MASK       0xffffffffULL

#define MAX_TABLES      8
#define MAX_FWMAPS      8
#define MAX_BUF_BYTES   (4ULL << 20)

namespace {

struct Table {
    uint64_t phys;
    IOMemoryDescriptor *md;
    IOMemoryMap *map;
    uint64_t *va;
};

struct FwMap {
    void *kva;
    IOMemoryDescriptor *md;
    IOMemoryMap *map;
};

IOLock *gLock;
bool gOpen;
volatile uint8_t *gRegs;
uint32_t gSid;
uint64_t gDramMask; /* asc-dram-mask: what the firmware ORs onto a DART address */
uint64_t gL1Phys;
uint64_t *gL1;
Table gTables[MAX_TABLES];
FwMap gFwMaps[MAX_FWMAPS];

int gSlot = -1; /* first-level slot of our window */
IOBufferMemoryDescriptor *gL2MD;
uint64_t *gL2;
uint64_t gL2Phys;
unsigned gNextPte; /* bump allocator inside our second-level table */

void dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void dlog(const char *fmt, ...)
{
    char line[400];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sf_rtkit_platform_log(line);
}

inline uint32_t rd(uint32_t off)
{
    uint32_t v = OSReadLittleInt32((volatile void *)gRegs, off);
    OSSynchronizeIO();
    return v;
}

inline void wr(uint32_t off, uint32_t v)
{
    OSWriteLittleInt32((volatile void *)gRegs, off, v);
    OSSynchronizeIO();
}

/*
 * One page of RAM by physical address. The tables iBoot and m1n1 left may lie outside the
 * memory this kernel manages; this is the mapping the Linux layer's memremap(MEMREMAP_WB)
 * makes for the GPU's page tables (sfos_map_phys with the default cache mode).
 */
bool mapPhys(uint64_t phys, uint64_t size, IOMemoryDescriptor **md, IOMemoryMap **map)
{
    *md = IOMemoryDescriptor::withPhysicalAddress((IOPhysicalAddress)phys, (IOByteCount)size,
                                                  kIODirectionNone | kIOMemoryMapperNone);
    *map = *md ? (*md)->map(kIOMapAnywhere) : NULL;
    if (!*map || !(*map)->getVirtualAddress()) {
        OSSafeReleaseNULL(*map);
        OSSafeReleaseNULL(*md);
        return false;
    }
    return true;
}

/* Whether a physical address lies in the range this kernel was given as its memory. */
const char *whose(uint64_t phys)
{
    boot_args *args = (boot_args *)PE_state.bootArgs;

    return phys >= args->physBase && phys - args->physBase < args->memSize ? "inside kernel memory" : "outside kernel memory";
}

/* A table page, mapped once and kept. */
uint64_t *tableAt(uint64_t phys)
{
    Table *free = NULL;

    /*
     * Our own table is reached through the descriptor that owns it: mapping a page this
     * kernel manages by physical address drew "IOMemoryDescriptor physical with managed
     * page" in boot dc11.
     */
    if (gL2 && phys == gL2Phys)
        return gL2;

    for (int i = 0; i < MAX_TABLES; i++) {
        if (gTables[i].va && gTables[i].phys == phys)
            return gTables[i].va;
        if (!gTables[i].va && !free)
            free = &gTables[i];
    }
    if (!free || (phys & (PAGE_16K - 1)))
        return NULL;
    if (!mapPhys(phys, PAGE_16K, &free->md, &free->map))
        return NULL;
    free->phys = phys;
    free->va = (uint64_t *)free->map->getVirtualAddress();
    return free->va;
}

/* Read-only walk. Returns the leaf entry (0 when the first level is empty). */
uint64_t walk(uint64_t iova, uint64_t *l1e, uint64_t *l2phys, bool *l2mapped)
{
    unsigned l1i = (unsigned)(iova >> 25), l2i = (unsigned)(iova >> PAGE_SHIFT_16K) & (PTES - 1);

    *l1e = 0;
    *l2phys = 0;
    *l2mapped = false;
    if (l1i >= PTES)
        return 0;
    *l1e = gL1[l1i];
    if (!(*l1e & PTE_VALID))
        return 0;
    *l2phys = *l1e & PTE_PADDR_MASK & ~(PAGE_16K - 1);
    uint64_t *l2 = tableAt(*l2phys);
    if (!l2)
        return 0;
    *l2mapped = true;
    return l2[l2i];
}

int invalidate()
{
    /* the table writes must be in memory before the DART is told to look again */
    __asm__ volatile("dsb sy" ::: "memory");
    wr(DART_STREAM_SELECT, 1u << gSid);
    wr(DART_STREAM_COMMAND, DART_STREAM_COMMAND_INVALIDATE);
    for (int i = 0; i < 1000; i++) {
        if (!(rd(DART_STREAM_COMMAND) & DART_STREAM_COMMAND_BUSY))
            return 0;
        IODelay(100);
    }
    return -ETIMEDOUT;
}

uint32_t dtU32(const char *path, const char *prop, bool *found)
{
    IORegistryEntry *e = IORegistryEntry::fromPath(path, gIODTPlane);
    OSData *d = e ? OSDynamicCast(OSData, e->getProperty(prop)) : NULL;
    uint32_t v = 0;

    *found = d && d->getLength() >= 4;
    if (*found)
        memcpy(&v, d->getBytesNoCopy(), 4);
    OSSafeReleaseNULL(e);
    return v;
}

uint64_t dtU64(const char *path, const char *prop, bool *found)
{
    IORegistryEntry *e = IORegistryEntry::fromPath(path, gIODTPlane);
    OSData *d = e ? OSDynamicCast(OSData, e->getProperty(prop)) : NULL;
    uint64_t v = 0;

    *found = d && d->getLength() >= 8;
    if (*found)
        memcpy(&v, d->getBytesNoCopy(), 8);
    OSSafeReleaseNULL(e);
    return v;
}

bool compatibleIs(IORegistryEntry *e, const char *want)
{
    OSData *d = e ? OSDynamicCast(OSData, e->getProperty("compatible")) : NULL;
    size_t n = strlen(want) + 1;

    return d && d->getLength() >= n && !memcmp(d->getBytesNoCopy(), want, n);
}

int stop(int err, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int stop(int err, const char *fmt, ...)
{
    char why[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(why, sizeof(why), fmt, ap);
    va_end(ap);
    dlog("dcp dart-stop: %s; the DART is not used", why);
    return err;
}

} // namespace

extern "C" {

int sf_dcp_dart_open(void)
{
    if (gOpen)
        return 0;
    if (!gLock)
        gLock = IOLockAlloc();
    if (!gLock)
        return -ENOMEM;

    IORegistryEntry *node = IORegistryEntry::fromPath(DART_NODE, gIODTPlane);
    if (!node)
        return stop(-ENOENT, "no " DART_NODE " in the device tree");
    if (!compatibleIs(node, "dart,t8020")) {
        node->release();
        return stop(-ENODEV, DART_NODE " is not a dart,t8020");
    }
    /* registry-owned array: used here, never released; this function gets here once */
    OSArray *regs = IODTResolveAddressing(node, "reg", NULL);
    IOMemoryDescriptor *reg = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
    uint64_t base = reg ? reg->getPhysicalSegment(0, NULL, kIOMemoryMapperNone) : 0;
    uint64_t len = reg ? reg->getLength() : 0;
    node->release();
    if (!base || len < 0x4000)
        return stop(-EINVAL, DART_NODE " reg 0 not resolved (0x%llx len 0x%llx)", (unsigned long long)base,
                    (unsigned long long)len);

    bool found;
    gSid = dtU32(MAPPER_NODE, "reg", &found);
    if (!found || gSid >= 16)
        return stop(-EINVAL, "no stream number in " MAPPER_NODE);
    gDramMask = dtU64(NUB_NODE, "asc-dram-mask", &found);
    if (!found)
        gDramMask = 0;
    /* the mask must sit above the stream's 32-bit input range, or addresses cannot be told apart */
    if (gDramMask & IOVA_MASK)
        return stop(-EINVAL, "asc-dram-mask 0x%llx overlaps the 32-bit DART address range",
                    (unsigned long long)gDramMask);

    gRegs = (volatile uint8_t *)sf_rtkit_platform_ioremap(base, 0x4000);
    if (!gRegs)
        return stop(-ENOMEM, "registers at 0x%llx could not be mapped", (unsigned long long)base);

    uint32_t params1 = rd(DART_PARAMS1), config = rd(DART_CONFIG), tcr = rd(DART_TCR(gSid));
    uint32_t enabled = rd(DART_ENABLED_STREAMS), error = rd(DART_ERROR);
    uint32_t ttbr[4];
    for (int i = 0; i < 4; i++)
        ttbr[i] = rd(DART_TTBR(gSid, i));
    dlog("dcp dart-regs: " DART_NODE " at 0x%llx stream %u: params1 0x%08x config 0x%08x (%s) tcr 0x%08x ttbr 0x%08x 0x%08x 0x%08x 0x%08x enabled-streams 0x%08x error 0x%08x, asc-dram-mask 0x%llx",
         (unsigned long long)base, (unsigned)gSid, params1, config, (config & DART_CONFIG_LOCK) ? "locked" : "not locked", tcr,
         ttbr[0], ttbr[1], ttbr[2], ttbr[3], enabled, error, (unsigned long long)gDramMask);

    if (DART_PARAMS1_PAGE_SHIFT(params1) != PAGE_SHIFT_16K)
        return stop(-EINVAL, "page shift %u, expected 14", DART_PARAMS1_PAGE_SHIFT(params1));
    /* m1n1 dart.c:481-486: the firmware's stream must be enabled and translating, not bypassed */
    if (!(enabled & (1u << gSid)))
        return stop(-EINVAL, "stream %u is not enabled", (unsigned)gSid);
    if ((tcr & (DART_TCR_TRANSLATE_ENABLE | DART_TCR_BYPASS_DART | DART_TCR_BYPASS_DAPF)) != DART_TCR_TRANSLATE_ENABLE)
        return stop(-EINVAL, "stream %u is not translating (tcr 0x%08x)", (unsigned)gSid, tcr);
    if (!(ttbr[0] & DART_TTBR_VALID))
        return stop(-EINVAL, "ttbr 0 is not valid");

    gL1Phys = (uint64_t)(ttbr[0] & ~DART_TTBR_VALID) << DART_TTBR_SHIFT;
    gL1 = tableAt(gL1Phys);
    if (!gL1)
        return stop(-ENOMEM, "first-level table at 0x%llx could not be mapped", (unsigned long long)gL1Phys);

    /* the live root: how many entries, and the first few */
    char list[220];
    size_t n = 0;
    unsigned valid = 0;
    list[0] = '\0';
    for (unsigned i = 0; i < PTES; i++) {
        uint64_t e = gL1[i];
        if (!(e & PTE_VALID))
            continue;
        if (valid < 8 && n + 28 < sizeof(list))
            n += snprintf(list + n, sizeof(list) - n, " [%u]=0x%llx", i, (unsigned long long)e);
        valid++;
    }
    dlog("dcp dart-l1: root of stream %u at phys 0x%llx (from ttbr 0, %s), %u of %u entries valid:%s%s", (unsigned)gSid,
         (unsigned long long)gL1Phys, whose(gL1Phys), valid, PTES, list, valid > 8 ? " ..." : "");

    /* our window: the first empty first-level slot of m1n1's allocation range */
    char slots[160];
    n = 0;
    slots[0] = '\0';
    for (unsigned i = WINDOW_START / L2_SPAN; i < WINDOW_END / L2_SPAN; i++) {
        uint64_t e = gL1[i];
        n += snprintf(slots + n, sizeof(slots) - n, " [%u]=%s", i, (e & PTE_VALID) ? "used" : e ? "nonzero" : "empty");
        if (gSlot < 0 && e == 0)
            gSlot = (int)i;
    }
    if (gSlot < 0)
        return stop(-ENOSPC, "no empty first-level slot in iova 0x%llx..0x%llx:%s", WINDOW_START, WINDOW_END, slots);
    dlog("dcp dart-window: first-level slots of iova 0x%llx..0x%llx:%s; allocations will use slot %d (iova 0x%llx, firmware address 0x%llx); nothing written yet",
         WINDOW_START, WINDOW_END, slots, gSlot, (unsigned long long)(gSlot * L2_SPAN),
         (unsigned long long)(gSlot * L2_SPAN | gDramMask));

    gOpen = true;
    return 0;
}

uint64_t sf_dcp_dart_walk(const char *what, uint64_t fw_addr)
{
    uint64_t iova = fw_addr & IOVA_MASK, l1e, l2phys, pte;
    bool l2mapped;

    if (!gOpen)
        return 0;
    IOLockLock(gLock);
    pte = walk(iova, &l1e, &l2phys, &l2mapped);
    IOLockUnlock(gLock);
    if (!(l1e & PTE_VALID)) {
        dlog("dcp dart-walk: %s 0x%llx (iova 0x%llx): first-level [%u] = 0x%llx, not mapped", what,
             (unsigned long long)fw_addr, (unsigned long long)iova, (unsigned)(iova >> 25), (unsigned long long)l1e);
        return 0;
    }
    if (!l2mapped) {
        dlog("dcp dart-walk: %s 0x%llx (iova 0x%llx): first-level [%u] = 0x%llx, second-level table at 0x%llx could not be mapped",
             what, (unsigned long long)fw_addr, (unsigned long long)iova, (unsigned)(iova >> 25), (unsigned long long)l1e,
             (unsigned long long)l2phys);
        return 0;
    }
    uint64_t phys = (pte & PTE_VALID) ? (pte & PTE_PADDR_MASK & ~(PAGE_16K - 1)) | (iova & (PAGE_16K - 1)) : 0;
    dlog("dcp dart-walk: %s 0x%llx (iova 0x%llx): first-level [%u] = 0x%llx, second-level table 0x%llx [%u] = 0x%llx: %s 0x%llx",
         what, (unsigned long long)fw_addr, (unsigned long long)iova, (unsigned)(iova >> 25), (unsigned long long)l1e,
         (unsigned long long)l2phys, (unsigned)(iova >> PAGE_SHIFT_16K) & (PTES - 1), (unsigned long long)pte,
         (pte & PTE_VALID) ? "phys" : "not mapped, phys", (unsigned long long)phys);
    return phys;
}

int sf_dcp_dart_selfcheck(void)
{
    IORegistryEntry *nub = IORegistryEntry::fromPath(NUB_NODE, gIODTPlane);
    OSData *seg = nub ? OSDynamicCast(OSData, nub->getProperty("segment-ranges")) : NULL;
    int good = 0;

    if (!gOpen || !seg || seg->getLength() < 32 || seg->getLength() % 32) {
        dlog("dcp dart-selfcheck: no usable segment-ranges in " NUB_NODE);
        OSSafeReleaseNULL(nub);
        return 0;
    }
    /* { u64 phys, u64 iova, u64 remap, u32 size, u32 flags } per segment (m1n1 adt.h) */
    const uint8_t *p = (const uint8_t *)seg->getBytesNoCopy();
    for (unsigned off = 0; off < seg->getLength() && off < 6 * 32; off += 32) {
        uint64_t phys, remap, l1e, l2phys, pte;
        uint32_t size;
        bool l2mapped;

        memcpy(&phys, p + off, 8);
        memcpy(&remap, p + off + 16, 8);
        memcpy(&size, p + off + 24, 4);
        if (!size)
            continue;
        /*
         * Only segments the firmware reaches through the DART: their run address carries
         * the asc-dram-mask bits. (The first segment of this firmware has remap == phys.)
         */
        if (!gDramMask || (remap & gDramMask) != gDramMask) {
            dlog("dcp dart-selfcheck: firmware segment %u runs at 0x%llx, not a DART address; skipped", off / 32,
                 (unsigned long long)remap);
            continue;
        }
        uint64_t iova = remap & ~gDramMask & IOVA_MASK;
        IOLockLock(gLock);
        pte = walk(iova, &l1e, &l2phys, &l2mapped);
        IOLockUnlock(gLock);
        uint64_t got = (pte & PTE_VALID) ? (pte & PTE_PADDR_MASK & ~(PAGE_16K - 1)) | (iova & (PAGE_16K - 1)) : 0;
        bool ok = (pte & PTE_VALID) && got == phys;
        dlog("dcp dart-selfcheck: firmware segment %u runs at 0x%llx (iova 0x%llx), device tree phys 0x%llx size 0x%x: leaf 0x%llx -> phys 0x%llx: %s",
             off / 32, (unsigned long long)remap, (unsigned long long)iova, (unsigned long long)phys, size,
             (unsigned long long)pte, (unsigned long long)got,
             ok ? "matches" : (pte & PTE_VALID) ? "DIFFERS" : "not mapped");
        if (ok)
            good++;
    }
    OSSafeReleaseNULL(nub);
    return good;
}

void *sf_dcp_dart_map_fw(uint64_t fw_addr, size_t size)
{
    uint64_t iova = fw_addr & IOVA_MASK;
    uint64_t first = iova & ~(PAGE_16K - 1), last = (iova + size - 1) & ~(PAGE_16K - 1);
    uint64_t base = 0;
    void *kva = NULL;

    if (!gOpen || !size || size > MAX_BUF_BYTES || iova + size - 1 > IOVA_MASK) {
        dlog("dcp dart-fwbuf: firmware 0x%llx size 0x%zx: refused (DART not open, or size out of range)",
             (unsigned long long)fw_addr, size);
        return NULL;
    }
    IOLockLock(gLock);
    for (uint64_t page = first; page <= last; page += PAGE_16K) {
        uint64_t l1e, l2phys, pte;
        bool l2mapped;

        pte = walk(page, &l1e, &l2phys, &l2mapped);
        uint64_t phys = pte & PTE_PADDR_MASK & ~(PAGE_16K - 1);
        if (!(pte & PTE_VALID) || (page != first && phys != base + (page - first))) {
            IOLockUnlock(gLock);
            dlog("dcp dart-fwbuf: firmware 0x%llx size 0x%zx: page at iova 0x%llx %s (entry 0x%llx, first page phys 0x%llx); not mapped",
                 (unsigned long long)fw_addr, size, (unsigned long long)page,
                 (pte & PTE_VALID) ? "is not contiguous with the first" : "has no valid entry", (unsigned long long)pte,
                 (unsigned long long)base);
            return NULL;
        }
        if (page == first)
            base = phys;
    }
    FwMap *m = NULL;
    for (int i = 0; i < MAX_FWMAPS && !m; i++)
        if (!gFwMaps[i].kva)
            m = &gFwMaps[i];
    uint64_t span = last - first + PAGE_16K;
    if (m && mapPhys(base, span, &m->md, &m->map)) {
        kva = (uint8_t *)m->map->getVirtualAddress() + (iova - first);
        m->kva = kva;
    }
    IOLockUnlock(gLock);
    if (!kva) {
        dlog("dcp dart-fwbuf: firmware 0x%llx size 0x%zx: phys 0x%llx could not be mapped into the kernel",
             (unsigned long long)fw_addr, size, (unsigned long long)base);
        return NULL;
    }
    dlog("dcp dart-fwbuf: firmware 0x%llx size 0x%zx: iova 0x%llx -> phys 0x%llx (%s), %llu pages contiguous, mapped for reading; no DART write",
         (unsigned long long)fw_addr, size, (unsigned long long)iova, (unsigned long long)(base + (iova - first)), whose(base),
         (unsigned long long)(span >> PAGE_SHIFT_16K));
    return kva;
}

void sf_dcp_dart_unmap_fw(void *kva)
{
    if (!kva || !gLock)
        return;
    IOLockLock(gLock);
    for (int i = 0; i < MAX_FWMAPS; i++) {
        if (gFwMaps[i].kva == kva) {
            OSSafeReleaseNULL(gFwMaps[i].map);
            OSSafeReleaseNULL(gFwMaps[i].md);
            gFwMaps[i].kva = NULL;
            break;
        }
    }
    IOLockUnlock(gLock);
}

void *sf_dcp_dart_alloc(size_t size, uint64_t *fw_addr)
{
    size_t bytes = (size + PAGE_16K - 1) & ~(size_t)(PAGE_16K - 1);
    unsigned pages = (unsigned)(bytes >> PAGE_SHIFT_16K);
    IOBufferMemoryDescriptor *md = NULL;
    void *kva = NULL;

    if (!gOpen || !size || bytes > MAX_BUF_BYTES || !fw_addr) {
        dlog("dcp dart-map: 0x%zx bytes: refused (DART not open, or size out of range)", size);
        return NULL;
    }
    IOLockLock(gLock);
    do {
        if (gNextPte + pages > PTES) {
            dlog("dcp dart-map: 0x%zx bytes: our second-level table is full", size);
            break;
        }
        /* wired, contiguous, below 36 bits, zeroed: the firmware reads it as soon as it has the address */
        md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
                                                              bytes, 0x0000000fffffc000ULL);
        if (!md || md->prepare() != kIOReturnSuccess) {
            dlog("dcp dart-map: 0x%zx bytes: no memory", bytes);
            OSSafeReleaseNULL(md);
            break;
        }
        bzero(md->getBytesNoCopy(), bytes);
        uint64_t pa = md->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
        if (!pa || (pa & (PAGE_16K - 1)) || ((pa + bytes - 1) & ~PTE_PADDR_MASK & ~(PAGE_16K - 1))) {
            dlog("dcp dart-map: buffer has unusable physical address 0x%llx", (unsigned long long)pa);
            break;
        }

        if (!gL2) {
            /* first use: our second-level table, then the one write into the live root */
            gL2MD = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
                kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous, PAGE_16K, 0x0000000fffffc000ULL);
            if (!gL2MD || gL2MD->prepare() != kIOReturnSuccess) {
                dlog("dcp dart-l2: no memory for a second-level table");
                OSSafeReleaseNULL(gL2MD);
                break;
            }
            uint64_t *l2 = (uint64_t *)gL2MD->getBytesNoCopy();
            bzero(l2, PAGE_16K);
            gL2Phys = gL2MD->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
            uint64_t before = gL1[gSlot];
            if (!gL2Phys || (gL2Phys & ~PTE_PADDR_MASK) || (gL2Phys & (PAGE_16K - 1)) || before != 0) {
                dlog("dcp dart-l2: not installed: table phys 0x%llx, first-level [%d] now reads 0x%llx (must be 0); nothing written",
                     (unsigned long long)gL2Phys, gSlot, (unsigned long long)before);
                break;
            }
            __asm__ volatile("dsb sy" ::: "memory");
            gL1[gSlot] = gL2Phys | PTE_VALID;
            __asm__ volatile("dsb sy" ::: "memory");
            uint64_t after = gL1[gSlot];
            gL2 = l2;
            dlog("dcp dart-l2: WRITE first-level [%d] at phys 0x%llx: 0x%llx -> 0x%llx (our empty second-level table at phys 0x%llx), read back 0x%llx",
                 gSlot, (unsigned long long)(gL1Phys + 8 * gSlot), (unsigned long long)before,
                 (unsigned long long)(gL2Phys | PTE_VALID), (unsigned long long)gL2Phys, (unsigned long long)after);
            if (after != (gL2Phys | PTE_VALID)) {
                dlog("dcp dart-l2: read back differs; not continuing");
                gL2 = NULL;
                break;
            }
        }

        unsigned firstPte = gNextPte;
        uint64_t iova = gSlot * L2_SPAN + ((uint64_t)firstPte << PAGE_SHIFT_16K);
        bool ok = true;
        for (unsigned i = 0; i < pages && ok; i++) {
            uint64_t want = ((pa + ((uint64_t)i << PAGE_SHIFT_16K)) & PTE_PADDR_MASK) | PTE_SP_END | PTE_SP_DIS | PTE_VALID;
            if (gL2[firstPte + i] != 0) {
                ok = false;
                break;
            }
            gL2[firstPte + i] = want;
        }
        if (!ok) {
            for (unsigned i = 0; i < pages; i++)
                gL2[firstPte + i] = 0;
            dlog("dcp dart-map: a leaf slot of ours was not empty; entries cleared again");
            break;
        }
        gNextPte += pages;
        dlog("dcp dart-map: WRITE %u leaf entr%s in our table, [%u..%u]: iova 0x%llx -> phys 0x%llx, first entry 0x%llx, read back 0x%llx",
             pages, pages == 1 ? "y" : "ies", firstPte, firstPte + pages - 1, (unsigned long long)iova, (unsigned long long)pa,
             (unsigned long long)((pa & PTE_PADDR_MASK) | PTE_SP_END | PTE_SP_DIS | PTE_VALID), (unsigned long long)gL2[firstPte]);

        int ret = invalidate();
        dlog("dcp dart-tlb: WRITE stream-select 0x%x, stream-command invalidate: %s", 1u << gSid,
             ret ? "BUSY did not clear in 100 ms" : "done");

        uint64_t l1e, l2phys, pte;
        bool l2mapped;
        pte = walk(iova, &l1e, &l2phys, &l2mapped);
        uint32_t err = rd(DART_ERROR);
        bool good = !ret && (pte & PTE_VALID) && (pte & PTE_PADDR_MASK) == (pa & PTE_PADDR_MASK) && l2phys == gL2Phys;
        dlog("dcp dart-verify: walk of iova 0x%llx through the live root: first-level 0x%llx, leaf 0x%llx -> phys 0x%llx: %s; error register 0x%08x",
             (unsigned long long)iova, (unsigned long long)l1e, (unsigned long long)pte,
             (unsigned long long)(pte & PTE_PADDR_MASK), good ? "matches" : "DOES NOT MATCH", err);
        if (!good) {
            for (unsigned i = 0; i < pages; i++)
                gL2[firstPte + i] = 0;
            invalidate();
            dlog("dcp dart-map: leaf entries cleared again and invalidated; no address is given to the firmware");
            break;
        }
        *fw_addr = iova | gDramMask;
        kva = md->getBytesNoCopy();
        md = NULL; /* kept until reboot: the firmware may use it for as long as it runs */
    } while (false);
    IOLockUnlock(gLock);
    if (md) {
        md->complete();
        md->release();
    }
    return kva;
}

void sf_dcp_dart_release(uint64_t fw_addr, size_t size)
{
    uint64_t iova = fw_addr & IOVA_MASK;
    unsigned pages = (unsigned)(((size + PAGE_16K - 1) & ~(size_t)(PAGE_16K - 1)) >> PAGE_SHIFT_16K);

    if (!gOpen || !gL2 || gSlot < 0 || iova / L2_SPAN != (uint64_t)gSlot)
        return;
    unsigned first = (unsigned)(iova >> PAGE_SHIFT_16K) & (PTES - 1);
    IOLockLock(gLock);
    for (unsigned i = 0; i < pages && first + i < PTES; i++)
        gL2[first + i] = 0;
    int ret = invalidate();
    IOLockUnlock(gLock);
    dlog("dcp dart-unmap: WRITE %u leaf entries of ours cleared (iova 0x%llx), invalidate %s; the memory is kept",
         pages, (unsigned long long)iova, ret ? "timed out" : "done");
}

uint32_t sf_dcp_dart_check(const char *when)
{
    if (!gOpen)
        return 0;
    uint32_t err = rd(DART_ERROR);
    if (err & DART_ERROR_FLAG)
        dlog("dcp dart-error: (%s) error 0x%08x stream %u code 0x%06x address 0x%08x%08x; not cleared", when, err,
             (err >> 24) & 0xf, err & 0xffffff, rd(DART_ERROR_ADDR_HI), rd(DART_ERROR_ADDR_LO));
    return err;
}

} // extern "C"
