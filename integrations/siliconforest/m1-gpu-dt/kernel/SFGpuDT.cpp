/* SPDX-License-Identifier: MIT */
/*
 * SFGpuDT: feeds src/gpudt.c from Apple's device tree in gIODTPlane.
 *
 * Boot arguments:
 *   sfgpudt=0        do nothing
 *   sfgpudt=1        (default) power GFX, read the core mask, build, log a summary
 *   sfgpudt=2        the same, then publish the nodes in gIODTPlane
 *   sfgpudt_power=0  never write PMGR. The core mask is then read only if GFX
 *                    happens to be on already; otherwise 8 cores are assumed
 *                    for the log and nothing is published.
 *   sfgpudt_stringprops=1  the of layer honours "pd,string-props"
 *                    (sf_linux_of.patch change 1); without it sfgpudt=2
 *                    refuses to publish a tree that layer would misread
 *
 * Order of operations in start():
 *   1. refuse anything but T8103 (/arm-io compatible "arm-io,t8103")
 *   2. PMGR: for /arm-io/sgx, then /arm-io/gfx-asc, walk "clock-gates";
 *      for each gate enable its parents first, then the device itself unless
 *      it is virtual. On this SoC every gate named there is virtual and the
 *      only register written is GFX (device 99, 0x23b7003f8). Logged before
 *      and after. GFX is left on.
 *   3. read and log the GPU ID registers and the core mask (sgx reg[0] + 0xd01500), only
 *      with GFX active
 *   4. run the library on /arm-io/sgx, /arm-io/gfx-asc, /arm-io and /chosen
 *   5. log the summary (the host test prints the same lines:
 *      `make test`, "predicted SFGpuDT kext log")
 *   6. sfgpudt=2: publish under the device-tree root
 *        /gpu@206400000                 IOPlatformDevice, the node the Linux
 *                                       platform driver binds to
 *        /reserved-memory/{uat-ttbs@30,uat-pagetables@20,uat-handoff@10,
 *                          hw-cal-a,hw-cal-b,globals}
 *        /opp-table-gpu/opp00..opp06
 *      The root has #address-cells = #size-cells = 2 and no "ranges", so
 *      "reg" holds CPU physical addresses. Apple's /arm-io/sgx is untouched.
 *
 * The PMGR code is ported from m1n1 src/pmgr.c (MIT, Copyright The Asahi Linux
 * Contributors): pmgr_init() without its pass over every device,
 * pmgr_adt_power_enable(), pmgr_set_mode_recursive(), pmgr_set_mode().
 *
 * Compiled into SFGpuDT.kext by build-kext.nix. Not yet run on hardware.
 */
#include "SFGpuDT.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>

#define LOG(fmt, ...) IOLog("SFGpuDT: " fmt "\n", ##__VA_ARGS__)

/* SFDispDT.cpp: the display nodes, behind boot-arg sfdispdt (default off) */
bool sf_dispdt_run(const uint32_t *fwVersion, uint32_t fwVersionLen, bool ofHasStringProps);

#define super IOService
OSDefineMetaClassAndStructors(SFGpuDT, IOService);

/* PMGR GFX power-state register on T8103 (ADT pmgr device 99; Linux ps_gfx, reg 0x3f8). */
static const uint64_t kPmgrGfxPs = 0x23b7003f8ull;
static const uint16_t kPmgrGfxId = 99;

/* ---- small helpers -------------------------------------------------------- */

static OSData *
propData(IORegistryEntry *e, const char *name)
{
    return e ? OSDynamicCast(OSData, e->getProperty(name)) : NULL;
}

static uint32_t
le32(const uint8_t *p)
{
    return OSReadLittleInt32(p, 0);
}

static uint64_t
le64(const uint8_t *p)
{
    return OSReadLittleInt64(p, 0);
}

static bool
compatibleHas(IORegistryEntry *e, const char *want)
{
    OSData *d = propData(e, "compatible");
    const char *s = d ? (const char *)d->getBytesNoCopy() : NULL;
    uint32_t len = d ? d->getLength() : 0;

    for (uint32_t o = 0; s && o < len; o += (uint32_t)strnlen(s + o, len - o) + 1) {
        if (strnlen(s + o, len - o) == strlen(want) && strncmp(s + o, want, len - o) == 0)
            return true;
    }
    return false;
}

/* An address below /arm-io to a CPU physical address: ranges is (child, parent, size) as u64 each. */
static bool
armioTranslate(IORegistryEntry *armio, uint64_t *addr)
{
    OSData *r = propData(armio, "ranges");
    const uint8_t *p = r ? (const uint8_t *)r->getBytesNoCopy() : NULL;

    for (uint32_t off = 0; p && off + 24 <= r->getLength(); off += 24) {
        uint64_t child = le64(p + off), parent = le64(p + off + 8), size = le64(p + off + 16);
        if (*addr >= child && *addr - child < size) {
            *addr = *addr - child + parent;
            return true;
        }
    }
    return false;
}

/* Entry idx of a node's "reg" (u64 address, u64 size), as a CPU physical address. */
static bool
nodeReg(IORegistryEntry *armio, IORegistryEntry *node, uint32_t idx, uint64_t *addr, uint64_t *size)
{
    OSData *reg = propData(node, "reg");

    if (!reg || ((uint64_t)idx + 1) * 16 > reg->getLength())
        return false;
    const uint8_t *p = (const uint8_t *)reg->getBytesNoCopy() + 16 * idx;
    *addr = le64(p);
    if (size)
        *size = le64(p + 8);
    return armioTranslate(armio, addr);
}

/* One 32-bit register, mapped uncached for the object's life. */
struct Reg32 {
    IOMemoryDescriptor *md;
    IOMemoryMap *map;
    volatile uint32_t *va;

    Reg32() : md(NULL), map(NULL), va(NULL) {}
    ~Reg32()
    {
        OSSafeReleaseNULL(map);
        OSSafeReleaseNULL(md);
    }
    bool open(uint64_t phys)
    {
        const uint64_t page = 0x4000;
        uint64_t start = phys & ~(page - 1);

        md = IODeviceMemory::withRange((IOPhysicalAddress)start, (IOPhysicalLength)page);
        map = md ? md->map(kIOMapInhibitCache) : NULL;
        if (!map || !map->getVirtualAddress())
            return false;
        va = (volatile uint32_t *)(map->getVirtualAddress() + (phys - start));
        return true;
    }
    uint32_t read() const
    {
        uint32_t v = OSReadLittleInt32((volatile void *)va, 0);
        OSSynchronizeIO();
        return v;
    }
    void write(uint32_t v) const
    {
        OSWriteLittleInt32((volatile void *)va, 0, v);
        OSSynchronizeIO();
    }
};

/* ---- m1n1 src/pmgr.c ------------------------------------------------------- */

#define PMGR_AUTO_ENABLE      (1u << 28)
#define PMGR_WAS_CLKGATED     (1u << 9)
#define PMGR_WAS_PWRGATED     (1u << 8)
#define PMGR_PS_ACTUAL_SHIFT  4
#define PMGR_PS_MASK          0xfu
#define PMGR_PS_ACTIVE        0xfu
#define PMGR_POLL_TIMEOUT_US  10000
#define PMGR_FLAG_VIRTUAL     0x10
#define PMGR_MAX_PARENT_DEPTH 32
#define PMGR_DEVICE_ID_MASK   0xffffu
#define PMGR_DIE_SHIFT        28

/* struct pmgr_device, packed, 48 bytes: field offsets */
#define PMGR_DEV_SIZE        48
#define PMGR_DEV_FLAGS       0
#define PMGR_DEV_ID1         3
#define PMGR_DEV_PARENTS     4
#define PMGR_DEV_ADDR_OFFSET 10
#define PMGR_DEV_PSREG_IDX   11
#define PMGR_DEV_ID2         26
#define PMGR_DEV_NAME        32
#define PMGR_DEV_NAME_LEN    16

struct Pmgr {
    IORegistryEntry *armio, *pmgr;
    const uint8_t *devices, *psRegs;
    uint32_t ndevices, psRegsLen;
    bool u8id;
    bool allActive;   /* every non-virtual device walked reads ACTIVE */
    unsigned written; /* registers written */

    Pmgr() : armio(NULL), pmgr(NULL), devices(NULL), psRegs(NULL), ndevices(0), psRegsLen(0), u8id(false),
             allActive(true), written(0) {}
    ~Pmgr()
    {
        /* device-tree entries are never freed, so the property bytes outlive these references */
        OSSafeReleaseNULL(armio);
        OSSafeReleaseNULL(pmgr);
    }

    bool load()
    {
        uint32_t dies = 1;
        OSData *d;

        armio = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
        pmgr = IORegistryEntry::fromPath("/arm-io/pmgr", gIODTPlane);
        if (!armio || !pmgr) {
            LOG("pmgr: no /arm-io/pmgr in the device tree");
            return false;
        }
        d = propData(armio, "die-count");
        if (d && d->getLength() >= 4)
            dies = le32((const uint8_t *)d->getBytesNoCopy());
        if (dies != 1) {
            LOG("pmgr: die-count %u, only single-die SoCs are handled", dies);
            return false;
        }
        d = propData(pmgr, "ps-regs");
        if (!d || !d->getLength() || d->getLength() % 12) {
            LOG("pmgr: no usable ps-regs");
            return false;
        }
        psRegs = (const uint8_t *)d->getBytesNoCopy();
        psRegsLen = d->getLength();
        d = propData(pmgr, "devices");
        if (!d || !d->getLength() || d->getLength() % PMGR_DEV_SIZE) {
            LOG("pmgr: no usable devices table");
            return false;
        }
        devices = (const uint8_t *)d->getBytesNoCopy();
        ndevices = d->getLength() / PMGR_DEV_SIZE;
        /* u8 or u16 ids: compare id1 of the first two devices */
        if (ndevices >= 2)
            u8id = devices[PMGR_DEV_ID1] != devices[PMGR_DEV_SIZE + PMGR_DEV_ID1];
        return true;
    }

    uint16_t devId(const uint8_t *dev) const
    {
        return u8id ? dev[PMGR_DEV_ID1] : OSReadLittleInt16(dev, PMGR_DEV_ID2);
    }

    const uint8_t *find(uint16_t id) const
    {
        for (uint32_t i = 0; i < ndevices; i++) {
            if (devId(devices + i * PMGR_DEV_SIZE) == id)
                return devices + i * PMGR_DEV_SIZE;
        }
        return NULL;
    }

    /* pmgr_device_get_addr() for die 0 */
    uint64_t devAddr(const uint8_t *dev) const
    {
        uint8_t idx = dev[PMGR_DEV_PSREG_IDX];
        uint64_t base;

        if (((uint32_t)idx + 1) * 12 > psRegsLen)
            return 0;
        if (!nodeReg(armio, pmgr, le32(psRegs + 12 * idx), &base, NULL))
            return 0;
        return base + le32(psRegs + 12 * idx + 4) + ((uint64_t)dev[PMGR_DEV_ADDR_OFFSET] << 3);
    }

    /* pmgr_set_mode_recursive_inner() for target ACTIVE: parents first, then the device unless virtual */
    bool walk(uint16_t id, bool write, unsigned depth)
    {
        const uint8_t *dev = id && depth < PMGR_MAX_PARENT_DEPTH ? find(id) : NULL;
        uint16_t parents[2];
        char name[PMGR_DEV_NAME_LEN + 1];

        if (!dev) {
            LOG("pmgr: device %u is not usable (depth %u)", id, depth);
            return false;
        }
        if (u8id) {
            parents[0] = dev[PMGR_DEV_PARENTS];
            parents[1] = dev[PMGR_DEV_PARENTS + 1];
        } else {
            parents[0] = OSReadLittleInt16(dev, PMGR_DEV_PARENTS);
            parents[1] = OSReadLittleInt16(dev, PMGR_DEV_PARENTS + 2);
        }
        for (int i = 0; i < 2; i++) {
            if (parents[i] && !walk(parents[i], write, depth + 1))
                return false;
        }
        if (dev[PMGR_DEV_FLAGS] & PMGR_FLAG_VIRTUAL)
            return true;

        memcpy(name, dev + PMGR_DEV_NAME, PMGR_DEV_NAME_LEN);
        name[PMGR_DEV_NAME_LEN] = 0;
        uint64_t addr = devAddr(dev);
        /* the one register this kext is meant to write; anything else means the tables are not what was studied */
        if (id != kPmgrGfxId || addr != kPmgrGfxPs) {
            LOG("pmgr: %s(%u) at 0x%llx is not GFX(%u) at 0x%llx; not touched", name, id,
                (unsigned long long)addr, kPmgrGfxId, (unsigned long long)kPmgrGfxPs);
            allActive = false;
            return false;
        }
        Reg32 reg;
        if (!reg.open(addr)) {
            LOG("pmgr: could not map 0x%llx", (unsigned long long)addr);
            return false;
        }
        uint32_t before = reg.read(), after = before;
        bool active = ((before >> PMGR_PS_ACTUAL_SHIFT) & PMGR_PS_MASK) == PMGR_PS_ACTIVE;

        if (write && !active) {
            /* pmgr_set_mode(): clear the sticky and auto bits, target ACTIVE, wait for actual */
            reg.write((before & ~(PMGR_AUTO_ENABLE | PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED | PMGR_PS_MASK)) |
                      PMGR_PS_ACTIVE);
            written++;
            for (unsigned waited = 0;; waited += 10) {
                after = reg.read();
                active = ((after >> PMGR_PS_ACTUAL_SHIFT) & PMGR_PS_MASK) == PMGR_PS_ACTIVE;
                if (active || waited >= PMGR_POLL_TIMEOUT_US)
                    break;
                IODelay(10);
            }
        }
        LOG("pmgr %s(%u) at 0x%llx: 0x%08x -> 0x%08x%s", name, id, (unsigned long long)addr, before, after,
            active ? "" : write ? " TIMEOUT, not active" : " (not active, not written)");
        if (!active)
            allActive = false;
        return active || !write;
    }

    /* pmgr_adt_power_enable(path): every entry of the node's clock-gates */
    bool clockGates(const char *path, bool write)
    {
        IORegistryEntry *node = IORegistryEntry::fromPath(path, gIODTPlane);
        OSData *d = propData(node, "clock-gates");
        bool ok = d && d->getLength() && !(d->getLength() & 3);

        if (!ok)
            LOG("pmgr: no usable clock-gates on %s", path);
        for (uint32_t i = 0; ok && i < d->getLength() / 4; i++) {
            uint32_t gate = le32((const uint8_t *)d->getBytesNoCopy() + 4 * i);
            if (gate >> PMGR_DIE_SHIFT) {
                ok = false;
                break;
            }
            if (!walk((uint16_t)(gate & PMGR_DEVICE_ID_MASK), write, 0))
                ok = false; /* m1n1 keeps going over the remaining gates */
        }
        OSSafeReleaseNULL(node);
        return ok;
    }
};

/* ---- ADT accessor for the library ------------------------------------------ */

/* Entries looked up during one build; kept so the OSData bytes handed to the library stay valid. */
struct AdtCtx {
    IORegistryEntry *held[8];
    const char *path[8];
    unsigned n;
};

static const void *
adtGetprop(void *ctx, const char *nodePath, const char *name, uint32_t *len)
{
    AdtCtx *c = (AdtCtx *)ctx;
    IORegistryEntry *e = NULL;

    for (unsigned i = 0; i < c->n; i++) {
        if (strcmp(c->path[i], nodePath) == 0)
            e = c->held[i];
    }
    if (e == NULL) {
        if (c->n >= 8)
            return NULL;
        e = IORegistryEntry::fromPath(nodePath, gIODTPlane);
        if (e == NULL)
            return NULL;
        c->path[c->n] = nodePath; /* the library passes string literals */
        c->held[c->n++] = e;
    }
    /* XNU keeps every device-tree property as OSData holding the ADT's own little-endian bytes */
    OSData *d = OSDynamicCast(OSData, e->getProperty(name));
    if (d == NULL)
        return NULL;
    *len = d->getLength();
    return d->getBytesNoCopy();
}

static void
emitLine(void *, const char *line)
{
    LOG("%s", line);
}

/* ---- publishing -------------------------------------------------------------- */

/* "uat-ttbs@30" -> name "uat-ttbs", location "30": sfos_dt_name() joins them again */
static void
nameEntry(IORegistryEntry *e, const char *full)
{
    char name[48];
    const char *at = strchr(full, '@');

    if (at == NULL || (size_t)(at - full) >= sizeof(name)) {
        e->setName(full);
        return;
    }
    memcpy(name, full, at - full);
    name[at - full] = 0;
    e->setName(name);
    e->setLocation(at + 1, gIODTPlane);
}

bool
SFGpuDT::publish(const struct gpudt_tree *tree, bool ofHasStringProps)
{
    IORegistryEntry *made[GPUDT_NODE_COUNT] = {};
    IORegistryEntry *root = IORegistryEntry::fromPath("/", gIODTPlane);
    bool ok = root != NULL;

    /*
     * Without change 1 of sf_linux_of.patch the of layer guesses text from the bytes. Refuse rather
     * than let it hand the driver a wrong number.
     */
    for (uint32_t i = 0; ok && !ofHasStringProps && i < tree->nprops; i++) {
        const struct gpudt_prop *p = &tree->props[i];
        if (!(p->flags & GPUDT_F_INFO) && !gpudt_prop_iodt_roundtrips(p)) {
            char path[96];
            LOG("%s/%s would be misread by sf_linux_of.c; not publishing (see sfgpudt_stringprops)",
                gpudt_node_path(tree, (enum gpudt_node_id)p->node, path, sizeof(path)), p->name);
            ok = false;
        }
    }

    /* Entries first, properties set before attaching. */
    for (int id = 0; ok && id < GPUDT_NODE_COUNT; id++) {
        const struct gpudt_node *n = &tree->nodes[id];
        IORegistryEntry *e;

        if (!n->present || id == GPUDT_NODE_CHOSEN)
            continue;
        /* the node a driver binds to must be a service whose name matching looks at "compatible" */
        if (id == GPUDT_NODE_GPU) {
            IOPlatformDevice *dev = OSTypeAlloc(IOPlatformDevice);
            ok = dev != NULL && dev->init();
            e = dev;
        } else {
            e = OSTypeAlloc(IORegistryEntry);
            ok = e != NULL && e->init();
        }
        if (!ok) {
            OSSafeReleaseNULL(e);
            break;
        }
        made[id] = e;
        nameEntry(e, n->name);

        OSData *strings = OSData::withCapacity(64);
        for (uint32_t i = 0; i < tree->nprops; i++) {
            const struct gpudt_prop *p = &tree->props[i];
            uint8_t bytes[GPUDT_MAX_VALUE];

            if (p->node != id)
                continue;
            gpudt_prop_to_iodt(p, bytes);
            OSData *d = OSData::withBytes(bytes, p->len);
            if (d == NULL) {
                ok = false;
                break;
            }
            e->setProperty(p->name, d);
            d->release();
            if (p->type == GPUDT_T_STRINGS && strings != NULL)
                strings->appendBytes(p->name, (unsigned int)strlen(p->name) + 1);
        }
        /* which properties are text, so the of layer need not guess from the bytes */
        if (ok && strings != NULL && strings->getLength() != 0)
            e->setProperty("pd,string-props", strings);
        OSSafeReleaseNULL(strings);
    }

    /*
     * Attach in id order, parents before children (the ids are ordered that way).
     * of_get_next_child() walks children in the order they were attached, and the driver takes the
     * operating points in that order without sorting them.
     */
    for (int id = 0; ok && id < GPUDT_NODE_COUNT; id++) {
        if (made[id] == NULL)
            continue;
        int parent = tree->nodes[id].parent;
        ok = made[id]->attachToParent(parent < 0 ? root : made[parent], gIODTPlane);
    }

    if (ok) {
        IOService *gpu = OSDynamicCast(IOService, made[GPUDT_NODE_GPU]);
        ok = gpu != NULL && gpu->attach(getPlatform());
        if (ok)
            gpu->registerService();
    }
    if (!ok) {
        for (int id = GPUDT_NODE_COUNT - 1; id >= 0; id--) {
            if (made[id] != NULL)
                made[id]->detachAll(gIODTPlane);
        }
    }
    for (int id = 0; id < GPUDT_NODE_COUNT; id++)
        OSSafeReleaseNULL(made[id]);
    OSSafeReleaseNULL(root);
    return ok;
}

bool
SFGpuDT::start(IOService *provider)
{
    int mode = 1, power = 1, stringProps = 0;
    struct gpudt_config cfg = {};
    struct gpudt_tree *tree;
    AdtCtx ctx = {};
    struct gpudt_adt_ops ops = {&ctx, adtGetprop};
    Pmgr pmgr;
    bool published = false;
    int ret;

    PE_parse_boot_argn("sfgpudt", &mode, sizeof(mode));
    PE_parse_boot_argn("sfgpudt_power", &power, sizeof(power));
    PE_parse_boot_argn("sfgpudt_stringprops", &stringProps, sizeof(stringProps));
    if (mode <= 0 || !super::start(provider))
        return false;
    LOG("start: sfgpudt=%d sfgpudt_power=%d sfgpudt_stringprops=%d", mode, power, stringProps);

    /* 1. this SoC only */
    if (!pmgr.load() || !compatibleHas(pmgr.armio, "arm-io,t8103")) {
        LOG("not a T8103 device tree; nothing done");
        return false;
    }

    /* 2. power: /arm-io/sgx first, then /arm-io/gfx-asc, as m1n1 does before it touches either */
    bool gatesOk = pmgr.clockGates("/arm-io/sgx", power != 0);
    gatesOk = pmgr.clockGates("/arm-io/gfx-asc", power != 0) && gatesOk;
    LOG("pmgr: %u register write(s), GFX %s", pmgr.written,
        gatesOk && pmgr.allActive ? "active" : "NOT active");

    /* 3. the core mask, only with the domain on: an unpowered SGX register faults */
    cfg.chip_id = GPUDT_CHIP_T8103;
    cfg.phandle_base = GPUDT_DARWIN_PHANDLE_BASE;
    /*
     * mboxes and power-domains name nodes of the Linux tree (mbox@206408000, the PMGR ps_gfx child)
     * that do not exist here: RTKit/mailbox and power come from native interfaces.
     */
    cfg.mbox_phandle = 0;
    cfg.power_domain_phandle = 0;
    if (gatesOk && pmgr.allActive) {
        IORegistryEntry *sgx = IORegistryEntry::fromPath("/arm-io/sgx", gIODTPlane);
        uint64_t base = 0, block1 = 0;
        uint32_t v[11];
        bool ok = sgx && nodeReg(pmgr.armio, sgx, 0, &base, NULL);

        /*
         * The registers m1n1 and the Linux driver identify the GPU by, all in the block at
         * sgx + 0xd00000 (the ADT's second sgx reg entry): eight ID words at +0x4000, the core
         * masks at +0x1500 and +0x1514, and +0x8024, which holds ADT ttbat-phys-addr-base.
         */
        static const uint32_t offs[11] = {0x4000, 0x4004, 0x4008, 0x400c, 0x4010, 0x4014,
                                          0x4018, 0x401c, 0x1500, 0x1514, 0x8024};
        for (unsigned i = 0; ok && i < 11; i++) {
            Reg32 reg;
            ok = reg.open(base + 0xd00000 + offs[i]);
            if (ok)
                v[i] = reg.read();
        }
        if (ok) {
            uint32_t ttbat = 0;
            OSData *d = propData(sgx, "ttbat-phys-addr-base");
            if (d && d->getLength() == 4)
                ttbat = le32((const uint8_t *)d->getBytesNoCopy());
            if (!nodeReg(pmgr.armio, sgx, 1, &block1, NULL) || block1 != base + 0xd00000)
                LOG("note: ADT sgx reg[1] is 0x%llx, not 0x%llx", (unsigned long long)block1,
                    (unsigned long long)(base + 0xd00000));
            LOG("sgx 0x%llx id +0x4000: %08x %08x %08x %08x %08x %08x %08x %08x",
                (unsigned long long)(base + 0xd00000), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            LOG("sgx core mask +0x1500 0x%08x +0x1514 0x%08x; +0x8024 0x%08x (ADT ttbat-phys-addr-base 0x%08x)",
                v[8], v[9], v[10], ttbat);
            cfg.have_core_mask = true;
            cfg.core_mask = v[8] & ((1u << GPUDT_T8103_MAX_CORES) - 1);
        } else {
            LOG("could not map the sgx identification registers");
        }
        OSSafeReleaseNULL(sgx);
    }
    if (!cfg.have_core_mask) {
        cfg.assume_all_cores = true;
        LOG("core mask NOT read: 8 cores assumed for this log, nothing will be published");
    }

    /* 4. the library. The tree is about 8 KiB: not for a kernel stack. */
    tree = (struct gpudt_tree *)IOMallocZero(sizeof(*tree));
    if (tree == NULL)
        return false;
    ret = gpudt_build_t8103(&ops, &cfg, tree);
    for (uint32_t i = 0; i < tree->ndiags; i++)
        LOG("note %u %s (0x%llx, 0x%llx)", tree->diags[i].code,
            tree->diags[i].text ? tree->diags[i].text : "", (unsigned long long)tree->diags[i].a,
            (unsigned long long)tree->diags[i].b);
    if (ret != GPUDT_OK) {
        LOG("the device tree was refused: %d", ret);
    } else {
        /* 5. */
        gpudt_summary(tree, emitLine, NULL);
        /* 6. */
        if (mode >= 2 && cfg.have_core_mask) {
            published = publish(tree, stringProps != 0);
            LOG("%s", published ? "published under the device-tree root" : "NOT published");
        } else {
            LOG("not publishing (sfgpudt=%d%s)", mode, cfg.have_core_mask ? "" : ", no core mask");
        }
    }
    /* the display nodes: independent of the GPU's being published, after them so that a shared
     * reserved-memory node is found; nothing unless sfdispdt is given */
    bool dispPublished = sf_dispdt_run(tree->fw_version, tree->fw_version_len, stringProps != 0);
    setProperty("SFDispDTPublished", dispPublished ? kOSBooleanTrue : kOSBooleanFalse);

    for (unsigned i = 0; i < ctx.n; i++)
        ctx.held[i]->release();
    IOFree(tree, sizeof(*tree));
    setProperty("SFGpuDTPublished", published ? kOSBooleanTrue : kOSBooleanFalse);
    registerService();
    return true;
}
