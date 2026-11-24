// SPDX-License-Identifier: GPL-2.0
/*
 * Apple T8103 PCIe controller, PHY and port 0 link bring-up for SiliconForest
 * (J293 port 0, BCM4378), driven from the IORegistry device-tree plane.
 *
 * This file as a whole is distributed under GPL-2.0, like the rest of the kext.
 *
 * 1. Controller, PHY and port bring-up (PMGR power enable, tunables, fuses, PHY
 *    clock/reset, port APPCLK/PERST/ready, root-port DesignWare setup) is ported from
 *    m1n1 src/pcie.c, src/pmgr.c and src/tunables.c, which carry this notice:
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
 * 2. The port link sequence (PERST# GPIO, lane and port reference clocks, PERST
 *    release, interrupt mask, MSI doorbell, RID-to-stream table, LTSSM start) is ported
 *    from Linux drivers/pci/controller/pcie-apple.c (omacom/linux-aurora, GPL-2.0;
 *    apple_pcie_setup_refclk, apple_pcie_setup_link, apple_pcie_setup_port,
 *    apple_pcie_port_setup_irq):
 *      Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *      Copyright (C) 2021 Google LLC
 *      Copyright (C) 2021 Corellium LLC
 *      Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 *      Author: Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *      Author: Marc Zyngier <maz@kernel.org>
 *    with the GPIO pin register bits of Linux drivers/pinctrl/pinctrl-apple-gpio.c
 *    (GPL-2.0-only, Copyright (C) The Asahi Linux Contributors), in the order and with
 *    the deviations of m1-apcie-bridge/proxy_linkup.py: REFCLK/APPCLK clock gating stays
 *    disabled (CGDIS set) and CLKREQ# GPIO 150 is muxed to function 1.
 *
 * 2a. The port shutdown (SFApcieT8103PortShutdown) puts those two sources together in
 *    reverse: the port interrupt mask and LTSSM stop of Linux apple_pcie_port_teardown()
 *    and apple_pcie_tunnel_stop(), PERST# through the GPIO, then m1n1 pcie_shutdown()
 *    for the port (reset, APPCLK) and the PHY (clock requests), then the radio's power.
 *
 * 3. The radio power enable (Linux "pwren", SMC GPIO 13) is the SMC key write gP0d of
 *    m1n1 proxyclient/experiments/pcie_enable_devices.py, done by SFAppleSMC.cpp at the
 *    place Linux apple_pcie_setup_link() switches pwren on. Boot argument sfapciesmc=0
 *    skips all SMC access (the launcher must then have powered the radio).
 *
 * Every on-SoC register here is mapped non-posted (kIOMapInhibitCache).
 */
#include "SFApcieT8103Link.h"
#include "SFAppleSMC.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSIterator.h>
#include <pexpert/pexpert.h>

#define LOG(fmt, ...) IOLog("SFApcieT8103: " fmt "\n", ##__VA_ARGS__)

#define BIT(n)        (1u << (n))
#define GENMASK(h, l) ((~0u >> (31 - (h))) & (~0u << (l)))

/* T8103 uses 16K pages; mappings are made on these boundaries. */
#define SOC_PAGE 0x4000ULL

/* ---- m1n1 src/pcie.c ------------------------------------------------------ */

/* ADT reg indices of /arm-io/apcie, regs_t8xxx_t600x */
#define APCIE_REG_CONFIG   0
#define APCIE_REG_RC       1
#define APCIE_REG_PHY      2
#define APCIE_REG_PHY_IP   3
#define APCIE_REG_AXI      4
#define APCIE_REG_FUSE     5
#define APCIE_SHARED_REGS  6

#define APCIE_PHY_CTRL             0x000
#define APCIE_PHY_CTRL_CLK0REQ     BIT(0)
#define APCIE_PHY_CTRL_CLK1REQ     BIT(1)
#define APCIE_PHY_CTRL_CLK0ACK     BIT(2)
#define APCIE_PHY_CTRL_CLK1ACK     BIT(3)
#define APCIE_PHY_CTRL_RESET_T8103 BIT(7)

#define APCIE_PHYIF_CTRL     0x024
#define APCIE_PHYIF_CTRL_RUN BIT(0)

#define APCIE_PORT_LINKSTS      0x208
#define APCIE_PORT_LINKSTS_UP   BIT(0)
#define APCIE_PORT_LINKSTS_BUSY BIT(2)
#define APCIE_PORT_APPCLK       0x800
#define APCIE_PORT_APPCLK_EN    BIT(0)
#define APCIE_PORT_STATUS       0x804
#define APCIE_PORT_STATUS_RUN   BIT(0)
#define APCIE_PORT_RESET        0x814
#define APCIE_PORT_RESET_DIS    BIT(0)

#define PCIE_CAP_BASE    0x70
#define PCIE_LNKCAP      0x0c
#define PCIE_LNKCAP_SLS  GENMASK(3, 0)
#define PCIE_LNKCAP_MLW  GENMASK(9, 4)
#define PCIE_LNKSTA      0x12
#define PCIE_LNKCAP2     0x2c
#define PCIE_LNKCAP2_SLS GENMASK(6, 1)
#define PCIE_LNKCTL2     0x30
#define PCIE_LNKCTL2_TLS GENMASK(3, 0)

#define DWC_DBI_RO_WR                    0x8bc
#define DWC_DBI_RO_WR_EN                 BIT(0)
#define DWC_DBI_PORT_LINK_CONTROL        0x710
#define DWC_DBI_PORT_LINK_MODE           GENMASK(21, 16)
#define DWC_DBI_PORT_LINK_MODE_1_LANE    0x1
#define DWC_DBI_LINK_WIDTH_SPEED_CONTROL 0x80c
#define DWC_DBI_LINK_WIDTH               GENMASK(12, 8)
#define DWC_DBI_SPEED_CHANGE             BIT(17)

/* One root port occupies 1 << 15 of the ECAM window (m1n1: config_base += 1 << 15). */
#define APCIE_CONFIG_PORT_SPAN 0x8000ULL

struct fuse_bits {
    UInt16 src_reg;
    UInt16 tgt_reg;
    UInt8 src_bit;
    UInt8 tgt_bit;
    UInt8 width;
};

static const struct fuse_bits pcie_fuse_bits_t8103[] = {
    {0x0084, 0x6238, 4, 0, 6},   {0x0084, 0x6220, 10, 14, 3}, {0x0084, 0x62a4, 13, 17, 2},
    {0x0418, 0x522c, 27, 9, 2},  {0x0418, 0x522c, 13, 12, 3}, {0x0418, 0x5220, 18, 14, 3},
    {0x0418, 0x52a4, 21, 17, 2}, {0x0418, 0x522c, 23, 16, 5}, {0x0418, 0x5278, 23, 20, 3},
    {0x0418, 0x5018, 31, 2, 1},  {0x041c, 0x1204, 0, 2, 5},   {0, 0, 0, 0, 0},
};

/* ---- m1n1 src/pmgr.c ------------------------------------------------------ */

#define PMGR_AUTO_ENABLE  BIT(28)
#define PMGR_WAS_CLKGATED BIT(9)
#define PMGR_WAS_PWRGATED BIT(8)
#define PMGR_PS_ACTUAL    GENMASK(7, 4)
#define PMGR_PS_TARGET    GENMASK(3, 0)
#define PMGR_PS_ACTIVE    0xf
#define PMGR_POLL_TIMEOUT 10000
#define PMGR_FLAG_VIRTUAL 0x10
#define PMGR_MAX_PARENT_DEPTH 32
#define PMGR_DIE_OFFSET   0x2000000000ULL
#define PMGR_DEVICE_ID    GENMASK(15, 0)
#define PMGR_DIE_SHIFT    28

/* struct pmgr_device (packed, 48 bytes): field offsets */
#define PMGR_DEV_SIZE        48
#define PMGR_DEV_FLAGS       0
#define PMGR_DEV_ID1         3
#define PMGR_DEV_PARENTS     4
#define PMGR_DEV_ADDR_OFFSET 10
#define PMGR_DEV_PSREG_IDX   11
#define PMGR_DEV_ID2         26
#define PMGR_DEV_NAME        32
#define PMGR_DEV_NAME_LEN    16

/* ---- Linux pcie-apple.c / pinctrl-apple-gpio.c ---------------------------- */

#define PHY_LANE_CFG              0x0
#define   PHY_LANE_CFG_REFCLK0REQ BIT(0)
#define   PHY_LANE_CFG_REFCLK1REQ BIT(1)
#define   PHY_LANE_CFG_REFCLK0ACK BIT(2)
#define   PHY_LANE_CFG_REFCLK1ACK BIT(3)
#define   PHY_LANE_CFG_REFCLKEN   (BIT(9) | BIT(10))
#define PHY_LANE_CTL              0x4
#define   PHY_LANE_CTL_CFGACC     BIT(15)
#define CORE_PHY_DEFAULT_BASE(port) (0x84000 + 0x4000 * (port))

#define PORT_LTSSMCTL          0x00080
#define   PORT_LTSSMCTL_START  BIT(0)
#define PORT_INTSTAT           0x00100
#define PORT_INTMSK            0x00104
#define PORT_MSICFG            0x00124
#define   PORT_MSICFG_EN       BIT(0)
#define   PORT_MSICFG_L2MSINUM_SHIFT 4
#define PORT_MSIBASE           0x00128
#define PORT_MSIADDR           0x00168
#define PORT_LINKSTS           0x00208
#define   PORT_LINKSTS_UP      BIT(0)
#define PORT_LINKCMDSTS        0x00210
#define PORT_APPCLK            0x00800
#define   PORT_APPCLK_EN       BIT(0)
#define   PORT_APPCLK_CGDIS    BIT(8)
#define PORT_STATUS            0x00804
#define   PORT_STATUS_READY    BIT(0)
#define PORT_REFCLK            0x00810
#define   PORT_REFCLK_EN       BIT(0)
#define   PORT_REFCLK_CGDIS    BIT(8)
#define PORT_PERST             0x00814
#define   PORT_PERST_OFF       BIT(0)
#define PORT_RID2SID           0x00828
#define MAX_RID2SID            64

/* Linux CONFIG_PCIE_APPLE_MSI_DOORBELL_ADDR default; J293 msi-address */
#define DOORBELL_ADDR 0xfffff000u
#define MSI_NVECS_LOG2 5 /* 32 vectors */

#define REG_GPIO(x)            (4 * (x))
#define REG_GPIOx_DATA         BIT(0)
#define REG_GPIOx_MODE         GENMASK(3, 1)
#define REG_GPIOx_OUT          1
#define REG_GPIOx_PERIPH       GENMASK(6, 5)
#define REG_GPIOx_INPUT_ENABLE BIT(9)
#define J293_PERST_PIN  152 /* pci-bridge0 function-perst; Linux reset-gpios pinctrl_ap 152 */
#define J293_CLKREQ_PIN 150 /* Linux t8103 pcie_pins: APPLE_PINMUX(150, 1) */
#define CLKREQ_FUNCTION 1
#define GPIO_MAP_LEN    SOC_PAGE

/* ---- register windows ----------------------------------------------------- */

namespace {

struct Mmio {
    IOMemoryDescriptor *md;
    IOMemoryMap *map;
    volatile UInt8 *va;
    UInt64 phys;
    UInt64 len;
    const char *name;

    Mmio() : md(NULL), map(NULL), va(NULL), phys(0), len(0), name("?") {}
    ~Mmio() { unmap(); }

    void unmap()
    {
        OSSafeReleaseNULL(map);
        OSSafeReleaseNULL(md);
        va = NULL;
    }

    /* Non-posted device mapping of [p, p + l). */
    bool mapPhys(const char *n, UInt64 p, UInt64 l)
    {
        unmap();
        name = n;
        if (!p || !l) {
            LOG("%s: no physical range to map", n);
            return false;
        }
        UInt64 start = p & ~(SOC_PAGE - 1);
        UInt64 span = ((p + l + SOC_PAGE - 1) & ~(SOC_PAGE - 1)) - start;
        md = IODeviceMemory::withRange((IOPhysicalAddress)start, (IOPhysicalLength)span);
        map = md ? md->map(kIOMapInhibitCache) : NULL;
        if (!map || !map->getVirtualAddress()) {
            LOG("%s: could not map 0x%llx len 0x%llx", n, (unsigned long long)p, (unsigned long long)l);
            unmap();
            return false;
        }
        va = (volatile UInt8 *)map->getVirtualAddress() + (p - start);
        phys = p;
        len = l;
        return true;
    }

    /* Entry idx of a resolved "reg" array; maxLen limits how much is mapped. */
    bool mapReg(const char *n, OSArray *regs, unsigned idx, UInt64 maxLen = 0)
    {
        IOMemoryDescriptor *d = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(idx)) : NULL;
        if (!d) {
            name = n;
            LOG("%s: no reg entry %u", n, idx);
            return false;
        }
        UInt64 p = d->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
        UInt64 l = d->getLength();
        if (maxLen && l > maxLen)
            l = maxLen;
        return mapPhys(n, p, l);
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
    /* m1n1 mask32(addr, clear, set) */
    void mask32(UInt64 off, UInt32 clear, UInt32 set) const { w32(off, (r32(off) & ~clear) | set); }
    void set32(UInt64 off, UInt32 bits) const { w32(off, r32(off) | bits); }
    void clear32(UInt64 off, UInt32 bits) const { w32(off, r32(off) & ~bits); }

    void mask8(UInt64 off, UInt8 clear, UInt8 set) const
    {
        volatile UInt8 *p = va + off;
        UInt8 v = *p;
        OSSynchronizeIO();
        *p = (UInt8)((v & ~clear) | set);
        OSSynchronizeIO();
    }
    void mask16(UInt64 off, UInt16 clear, UInt16 set) const
    {
        UInt16 v = OSReadLittleInt16((volatile void *)va, off);
        OSSynchronizeIO();
        OSWriteLittleInt16((volatile void *)va, off, (UInt16)((v & ~clear) | set));
        OSSynchronizeIO();
    }
    void mask64(UInt64 off, UInt64 clear, UInt64 set) const
    {
        UInt64 v = OSReadLittleInt64((volatile void *)va, off);
        OSSynchronizeIO();
        OSWriteLittleInt64((volatile void *)va, off, (v & ~clear) | set);
        OSSynchronizeIO();
    }
};

/*
 * m1n1 poll32(): wait until (reg & mask) == target, at most timeout_us. Busy-waits in
 * 10 us steps for the first 2 ms, then sleeps 1 ms per step. Always terminates.
 */
bool poll32(const Mmio &m, UInt64 off, UInt32 mask, UInt32 target, UInt32 timeout_us, UInt32 *last = NULL)
{
    UInt32 waited = 0;
    for (;;) {
        UInt32 v = m.r32(off);
        if (last)
            *last = v;
        if ((v & mask) == target)
            return true;
        if (waited >= timeout_us)
            return false;
        if (waited < 2000) {
            IODelay(10);
            waited += 10;
        } else {
            IOSleep(1);
            waited += 1000;
        }
    }
}

/* ---- device-tree helpers --------------------------------------------------- */

OSData *propData(IORegistryEntry *entry, const char *name)
{
    return entry ? OSDynamicCast(OSData, entry->getProperty(name)) : NULL;
}

bool propU32(IORegistryEntry *entry, const char *name, UInt32 *out)
{
    OSData *d = propData(entry, name);
    if (!d || d->getLength() < sizeof(UInt32))
        return false;
    memcpy(out, d->getBytesNoCopy(), sizeof(UInt32));
    return true;
}

UInt32 le32at(const UInt8 *p)
{
    UInt32 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

UInt16 le16at(const UInt8 *p)
{
    UInt16 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

bool isCompatible(IORegistryEntry *entry, const char *want)
{
    OSData *d = propData(entry, "compatible");
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

/* ---- m1n1 src/tunables.c: tunables_apply_local_addr() ---------------------- */

struct tunable_local {
    UInt32 offset;
    UInt32 size;
    UInt64 mask;
    UInt64 value;
} __attribute__((packed));

enum { kTunableOptional = 0, kTunableRequired = 1 };

/*
 * Apply the tunable_local list in `prop` of `node` to window `m`. m1n1 applies entry by
 * entry and checks nothing but the access size; here the whole list is checked against
 * the mapped window first, so a malformed list writes nothing.
 */
IOReturn applyTunables(IORegistryEntry *node, const char *prop, const Mmio &m, int required)
{
    OSData *d = propData(node, prop);
    if (!d || d->getLength() == 0) {
        if (!required) {
            LOG("tunables %s: not present, skipped", prop);
            return kIOReturnSuccess;
        }
        LOG("tunables %s: property missing", prop);
        return kIOReturnNotFound;
    }
    if (d->getLength() % sizeof(tunable_local)) {
        LOG("tunables %s: length %u is not a multiple of %u", prop, d->getLength(),
            (unsigned)sizeof(tunable_local));
        return kIOReturnBadArgument;
    }
    unsigned count = d->getLength() / sizeof(tunable_local);
    const UInt8 *raw = (const UInt8 *)d->getBytesNoCopy();
    tunable_local t;

    for (unsigned i = 0; i < count; i++) {
        memcpy(&t, raw + i * sizeof(t), sizeof(t));
        if (t.size != 1 && t.size != 2 && t.size != 4 && t.size != 8) {
            LOG("tunables %s: entry %u has unknown size 0x%08x", prop, i, t.size);
            return kIOReturnBadArgument;
        }
        if ((UInt64)t.offset + t.size > m.len) {
            LOG("tunables %s: entry %u offset 0x%x is outside %s (len 0x%llx)", prop, i, t.offset,
                m.name, (unsigned long long)m.len);
            return kIOReturnBadArgument;
        }
    }
    for (unsigned i = 0; i < count; i++) {
        memcpy(&t, raw + i * sizeof(t), sizeof(t));
        switch (t.size) {
        case 1:
            m.mask8(t.offset, (UInt8)t.mask, (UInt8)t.value);
            break;
        case 2:
            m.mask16(t.offset, (UInt16)t.mask, (UInt16)t.value);
            break;
        case 4:
            m.mask32(t.offset, (UInt32)t.mask, (UInt32)t.value);
            break;
        case 8:
            m.mask64(t.offset, t.mask, t.value);
            break;
        }
    }
    LOG("tunables %s: %u entries applied to %s (0x%llx)", prop, count, m.name, (unsigned long long)m.phys);
    return kIOReturnSuccess;
}

/* ---- m1n1 src/pmgr.c: pmgr_adt_power_enable() ------------------------------ */

struct Pmgr {
    OSArray *regs;        /* resolved /arm-io/pmgr "reg", retained */
    const UInt8 *devices; /* struct pmgr_device[] */
    unsigned ndevices;
    const UInt8 *psRegs; /* u32 { reg index, offset, mask }[] */
    unsigned psRegsLen;
    bool u8id;
    UInt32 dies;

    /* one line of evidence per power domain touched */
    struct Seen {
        UInt64 addr;
        UInt16 id;
        char name[PMGR_DEV_NAME_LEN + 1];
        UInt32 before, after;
    } seen[12];
    unsigned nseen;

    Pmgr() : regs(NULL), devices(NULL), ndevices(0), psRegs(NULL), psRegsLen(0), u8id(false), dies(1), nseen(0) {}
    ~Pmgr() { OSSafeReleaseNULL(regs); }

    /* pmgr_init(), without its write pass over every device (m1n1 has run that at boot) */
    IOReturn load()
    {
        IORegistryEntry *armio = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
        IORegistryEntry *pmgr = IORegistryEntry::fromPath("/arm-io/pmgr", gIODTPlane);
        IOReturn ret = kIOReturnSuccess;
        OSData *d;

        if (!propU32(armio, "die-count", &dies))
            dies = 1;
        if (!pmgr) {
            LOG("pmgr: no /arm-io/pmgr in the device tree");
            ret = kIOReturnNotFound;
        } else if (dies < 1 || dies > 16) {
            LOG("pmgr: bad die-count %u", dies);
            ret = kIOReturnBadArgument;
        }
        if (ret == kIOReturnSuccess) {
            d = propData(pmgr, "ps-regs");
            if (!d || !d->getLength() || d->getLength() % 12) {
                LOG("pmgr: no usable ps-regs (len %u); ps-groups tables are not supported",
                    d ? d->getLength() : 0);
                ret = kIOReturnUnsupported;
            } else {
                psRegs = (const UInt8 *)d->getBytesNoCopy();
                psRegsLen = d->getLength();
            }
        }
        if (ret == kIOReturnSuccess) {
            d = propData(pmgr, "devices");
            if (!d || !d->getLength() || d->getLength() % PMGR_DEV_SIZE) {
                LOG("pmgr: no usable devices table (len %u)", d ? d->getLength() : 0);
                ret = kIOReturnUnsupported;
            } else {
                devices = (const UInt8 *)d->getBytesNoCopy();
                ndevices = d->getLength() / PMGR_DEV_SIZE;
                /* u8 or u16 ids: compare id1 of the first two devices */
                if (ndevices >= 2)
                    u8id = devices[PMGR_DEV_ID1] != devices[PMGR_DEV_SIZE + PMGR_DEV_ID1];
            }
        }
        if (ret == kIOReturnSuccess) {
            regs = IODTResolveAddressing(pmgr, "reg", NULL);
            if (regs)
                regs->retain(); /* the registry owns the returned reference */
            if (!regs || !regs->getCount()) {
                LOG("pmgr: could not resolve the pmgr reg property");
                ret = kIOReturnNoResources;
            }
        }
        /* The property data stays valid: device-tree entries are never freed. */
        OSSafeReleaseNULL(pmgr);
        OSSafeReleaseNULL(armio);
        return ret;
    }

    UInt16 devId(const UInt8 *dev) const { return u8id ? dev[PMGR_DEV_ID1] : le16at(dev + PMGR_DEV_ID2); }

    void devParents(const UInt8 *dev, UInt16 parent[2]) const
    {
        if (u8id) {
            parent[0] = dev[PMGR_DEV_PARENTS];
            parent[1] = dev[PMGR_DEV_PARENTS + 1];
        } else {
            parent[0] = le16at(dev + PMGR_DEV_PARENTS);
            parent[1] = le16at(dev + PMGR_DEV_PARENTS + 2);
        }
    }

    const UInt8 *find(UInt16 id) const
    {
        for (unsigned i = 0; i < ndevices; i++) {
            const UInt8 *dev = devices + i * PMGR_DEV_SIZE;
            if (devId(dev) == id)
                return dev;
        }
        return NULL;
    }

    /* pmgr_device_get_addr(): physical address of the device's power-state register */
    UInt64 devAddr(UInt8 die, const UInt8 *dev) const
    {
        UInt8 idx = dev[PMGR_DEV_PSREG_IDX];
        if (die >= dies)
            return 0;
        if (((UInt32)idx + 1) * 12 > psRegsLen) {
            LOG("pmgr: index %u is out of bounds for ps-regs", idx);
            return 0;
        }
        UInt32 regIdx = le32at(psRegs + 12 * idx);
        UInt32 regOffset = le32at(psRegs + 12 * idx + 4);
        IOMemoryDescriptor *md = OSDynamicCast(IOMemoryDescriptor, regs->getObject(regIdx));
        if (!md) {
            LOG("pmgr: no pmgr reg entry %u", regIdx);
            return 0;
        }
        UInt64 addr = md->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
        if (!addr)
            return 0;
        return addr + regOffset + PMGR_DIE_OFFSET * die + ((UInt64)dev[PMGR_DEV_ADDR_OFFSET] << 3);
    }

    Seen *note(UInt64 addr, UInt16 id, const UInt8 *dev, UInt32 before)
    {
        for (unsigned i = 0; i < nseen; i++)
            if (seen[i].addr == addr)
                return &seen[i];
        if (nseen == sizeof(seen) / sizeof(seen[0]))
            return NULL;
        Seen *s = &seen[nseen++];
        s->addr = addr;
        s->id = id;
        memcpy(s->name, dev + PMGR_DEV_NAME, PMGR_DEV_NAME_LEN);
        s->name[PMGR_DEV_NAME_LEN] = 0;
        s->before = s->after = before;
        return s;
    }

    /*
     * pmgr_set_mode_recursive_inner() for a non-zero target mode: parents first, then
     * the device itself unless it is virtual. With write == false nothing is written and
     * *allActive reports whether every non-virtual device on the way is already ACTIVE.
     */
    IOReturn walk(UInt8 die, UInt16 id, bool write, bool *allActive, unsigned depth)
    {
        if (id == 0 || die >= dies || depth >= PMGR_MAX_PARENT_DEPTH) {
            LOG("pmgr: bad device %u die %u depth %u", id, die, depth);
            return kIOReturnBadArgument;
        }
        const UInt8 *dev = find(id);
        if (!dev) {
            LOG("pmgr: device %u is not in the devices table", id);
            return kIOReturnNotFound;
        }
        UInt16 parents[2];
        devParents(dev, parents);
        for (int i = 0; i < 2; i++) {
            if (parents[i]) {
                IOReturn ret = walk(die, parents[i], write, allActive, depth + 1);
                if (ret != kIOReturnSuccess)
                    return ret;
            }
        }
        if (dev[PMGR_DEV_FLAGS] & PMGR_FLAG_VIRTUAL)
            return kIOReturnSuccess;

        UInt64 addr = devAddr(die, dev);
        Mmio reg;
        if (!addr || !reg.mapPhys("pmgr ps", addr, 4))
            return kIOReturnNoResources;
        UInt32 value = reg.r32(0);
        Seen *s = note(addr, id, dev, value);
        if (!write) {
            if (((value & PMGR_PS_ACTUAL) >> 4) != PMGR_PS_ACTIVE)
                *allActive = false;
            return kIOReturnSuccess;
        }
        /* pmgr_set_mode() */
        reg.mask32(0, PMGR_AUTO_ENABLE | PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED | PMGR_PS_TARGET, PMGR_PS_ACTIVE);
        bool ok = poll32(reg, 0, PMGR_PS_ACTUAL, PMGR_PS_ACTIVE << 4, PMGR_POLL_TIMEOUT, &value);
        if (s)
            s->after = value;
        if (!ok) {
            LOG("pmgr: timeout setting %u active at 0x%llx: 0x%08x", id, (unsigned long long)addr, value);
            return kIOReturnTimeout;
        }
        return kIOReturnSuccess;
    }

    /* pmgr_adt_devices_set_mode(path, PMGR_PS_ACTIVE, recurse) over the node's clock-gates */
    IOReturn clockGates(IORegistryEntry *node, bool write, bool *allActive)
    {
        OSData *d = propData(node, "clock-gates");
        if (!d || !d->getLength() || (d->getLength() & 3)) {
            LOG("pmgr: no usable clock-gates on the apcie node");
            return kIOReturnNotFound;
        }
        const UInt8 *raw = (const UInt8 *)d->getBytesNoCopy();
        IOReturn result = kIOReturnSuccess;
        *allActive = true;
        for (unsigned i = 0; i < d->getLength() / 4; i++) {
            UInt32 entry = le32at(raw + 4 * i);
            IOReturn ret = walk((UInt8)(entry >> PMGR_DIE_SHIFT), (UInt16)(entry & PMGR_DEVICE_ID), write,
                                allActive, 0);
            if (ret != kIOReturnSuccess)
                result = ret; /* m1n1 keeps going over the remaining gates */
        }
        return result;
    }

    void logSeen(const char *what) const
    {
        char line[400];
        size_t used = 0;
        line[0] = 0;
        for (unsigned i = 0; i < nseen && used < sizeof(line); i++) {
            int n = snprintf(line + used, sizeof(line) - used, " %s(%u)@0x%llx 0x%08x->0x%08x", seen[i].name,
                             seen[i].id, (unsigned long long)seen[i].addr, seen[i].before, seen[i].after);
            if (n < 0)
                break;
            used += (size_t)n;
        }
        LOG("power domains %s:%s", what, line);
    }
};

/* ---- the bring-up ---------------------------------------------------------- */

struct Link {
    IORegistryEntry *apcie;
    IORegistryEntry *bridge;
    Mmio rc, phy, phyIp, axi, fuse, port, lane, config, gpio;
    Pmgr pmgr;
    UInt32 perstPin, clkreqPin;
    UInt32 linksts;
    UInt32 smcOptions; /* sfapciesmc boot argument, kSFAppleSMC* bits; 0 = no SMC access */

    Link() : apcie(NULL), bridge(NULL), perstPin(0), clkreqPin(0), linksts(0), smcOptions(kSFAppleSMCEnable) {}
    ~Link() { OSSafeReleaseNULL(bridge); }

    IOReturn setup(IORegistryEntry *node, OSArray *regs);
    IOReturn gpioSetup();
    bool controllerLooksUp(bool pmgrActive);
    IOReturn controllerInit();
    IOReturn portInit();
    IOReturn rootPortInit();
    IOReturn linkSequence();
    void shutdown();
    void gpioPerst(bool asserted);
};

/* Device-tree checks and mappings. Touches no register. */
IOReturn Link::setup(IORegistryEntry *node, OSArray *regs)
{
    UInt32 ports = 0;

    apcie = node;
    if (!isCompatible(apcie, "apcie,t8103")) {
        LOG("link: /arm-io/apcie is not apcie,t8103; no bring-up for this controller");
        return kIOReturnUnsupported;
    }
    if (!propU32(apcie, "#ports", &ports) || !regs) {
        LOG("link: apcie has no #ports or no resolved reg");
        return kIOReturnNotFound;
    }
    /* m1n1: ADT port geometry */
    unsigned entries = regs->getCount();
    unsigned perPort = 0;
    if (ports && ports <= 8 && entries >= APCIE_SHARED_REGS + 3 * ports &&
        (entries - APCIE_SHARED_REGS) % ports == 0)
        perPort = (entries - APCIE_SHARED_REGS) / ports;
    if (!perPort) {
        LOG("link: invalid port geometry (%u reg entries, %u ports)", entries, ports);
        return kIOReturnBadArgument;
    }
    bridge = IORegistryEntry::fromPath("/arm-io/apcie/pci-bridge0", gIODTPlane);
    if (!bridge) {
        LOG("link: no /arm-io/apcie/pci-bridge0 in the device tree");
        return kIOReturnNotFound;
    }

    unsigned port0 = APCIE_SHARED_REGS; /* port * perPort + shared, port 0 */
    if (!config.mapReg("root port config", regs, APCIE_REG_CONFIG, APCIE_CONFIG_PORT_SPAN) ||
        !rc.mapReg("rc", regs, APCIE_REG_RC) || !phy.mapReg("phy", regs, APCIE_REG_PHY) ||
        !phyIp.mapReg("phy ip", regs, APCIE_REG_PHY_IP) || !axi.mapReg("axi", regs, APCIE_REG_AXI) ||
        !fuse.mapReg("fuses", regs, APCIE_REG_FUSE) || !port.mapReg("port0", regs, port0) ||
        !lane.mapReg("port0 phy lane", regs, port0 + 2))
        return kIOReturnNoMemory;
    if (rc.len < 0x58 || phy.len < 0x4 || phyIp.len < 0x62a8 || fuse.len < 0x420 || port.len < 0x930 ||
        lane.len < 0x8 || config.len < APCIE_CONFIG_PORT_SPAN) {
        LOG("link: an apcie register window is shorter than the bring-up needs");
        return kIOReturnBadArgument;
    }
    if (lane.phys != rc.phys + CORE_PHY_DEFAULT_BASE(0)) {
        LOG("link: port0 PHY lane reg 0x%llx is not RC + 0x%x as Linux expects", (unsigned long long)lane.phys,
            CORE_PHY_DEFAULT_BASE(0));
        return kIOReturnBadArgument;
    }
    LOG("link: %u reg entries, %u per port; rc 0x%llx phy 0x%llx phy-ip 0x%llx port0 0x%llx lane 0x%llx", entries,
        perPort, (unsigned long long)rc.phys, (unsigned long long)phy.phys, (unsigned long long)phyIp.phys,
        (unsigned long long)port.phys, (unsigned long long)lane.phys);

    IOReturn ret = gpioSetup();
    if (ret != kIOReturnSuccess)
        return ret;
    return pmgr.load();
}

/* One "function-*" GPIO property: { phandle, 'GPIO', pin, flags }. */
static bool gpioFunctionPin(IORegistryEntry *node, const char *prop, UInt32 phandle, UInt32 *pin)
{
    OSData *d = propData(node, prop);
    if (!d || d->getLength() < 12)
        return false;
    const UInt8 *raw = (const UInt8 *)d->getBytesNoCopy();
    if (le32at(raw + 4) != 0x4750494f /* 'GPIO' */ || (phandle && le32at(raw) != phandle))
        return false;
    *pin = le32at(raw + 8);
    return true;
}

IOReturn Link::gpioSetup()
{
    IORegistryEntry *node = IORegistryEntry::fromPath("/arm-io/gpio", gIODTPlane);
    if (!node) {
        LOG("gpio: no /arm-io/gpio in the device tree");
        return kIOReturnNotFound;
    }
    UInt32 phandle = 0;
    propU32(node, "AAPL,phandle", &phandle);
    OSArray *regs = IODTResolveAddressing(node, "reg", NULL);
    bool mapped = regs && gpio.mapReg("gpio", regs, 0, GPIO_MAP_LEN);
    node->release();
    if (!mapped)
        return kIOReturnNoMemory;

    /* PERST# must be the pin the device tree names; the wrong pin would reset something else. */
    if (!gpioFunctionPin(bridge, "function-perst", phandle, &perstPin) || perstPin != J293_PERST_PIN) {
        LOG("gpio: pci-bridge0 function-perst does not name main GPIO %u (got %u)", J293_PERST_PIN, perstPin);
        return kIOReturnBadArgument;
    }
    /* CLKREQ#: Linux pcie_pins, pin 150 function 1. The device tree is only cross-checked. */
    clkreqPin = J293_CLKREQ_PIN;
    UInt32 dtClkreq = 0;
    if (gpioFunctionPin(bridge, "function-clkreq", phandle, &dtClkreq) && dtClkreq != clkreqPin)
        LOG("gpio: function-clkreq names pin %u; using %u as the proven sequence does", dtClkreq, clkreqPin);
    if (REG_GPIO(perstPin) + 4 > gpio.len || REG_GPIO(clkreqPin) + 4 > gpio.len)
        return kIOReturnBadArgument;
    return kIOReturnSuccess;
}

/*
 * Is the controller already in the state m1n1 pcie_init leaves it in? Reads are ordered
 * as m1n1 orders its own first accesses: PMGR, then the PHY block, then the RC, and the
 * port only once the PHY clocks are acknowledged.
 */
bool Link::controllerLooksUp(bool pmgrActive)
{
    if (!pmgrActive) {
        LOG("state: apcie power domains are not all active; port not read");
        return false;
    }
    UInt32 phyCtrl = phy.r32(APCIE_PHY_CTRL);
    UInt32 phyif = rc.r32(APCIE_PHYIF_CTRL);
    UInt32 clocks = APCIE_PHY_CTRL_CLK0REQ | APCIE_PHY_CTRL_CLK1REQ | APCIE_PHY_CTRL_CLK0ACK | APCIE_PHY_CTRL_CLK1ACK;
    if ((phyCtrl & (clocks | APCIE_PHY_CTRL_RESET_T8103)) != clocks || !(phyif & APCIE_PHYIF_CTRL_RUN)) {
        LOG("state: phy ctrl 0x%08x phyif ctl 0x%08x: PHY not running; port not read", phyCtrl, phyif);
        return false;
    }
    UInt32 appclk = port.r32(PORT_APPCLK), status = port.r32(PORT_STATUS);
    linksts = port.r32(PORT_LINKSTS);
    LOG("state: phy ctrl 0x%08x phyif ctl 0x%08x port0 appclk 0x%08x status 0x%08x refclk 0x%08x perst 0x%08x "
        "linksts 0x%08x",
        phyCtrl, phyif, appclk, status, port.r32(PORT_REFCLK), port.r32(PORT_PERST), linksts);
    return (appclk & PORT_APPCLK_EN) && (status & PORT_STATUS_READY);
}

/* m1n1 pcie_init_controller(), apcie,t8103 path, up to the per-port loop */
IOReturn Link::controllerInit()
{
    IOReturn ret;
    bool unused = true;
    UInt32 value = 0;

    ret = pmgr.clockGates(apcie, true, &unused);
    pmgr.logSeen(ret == kIOReturnSuccess ? "enabled" : "FAILED");
    if (ret != kIOReturnSuccess)
        return ret;

    if ((ret = applyTunables(apcie, "apcie-axi2af-tunables", axi, kTunableOptional)) != kIOReturnSuccess)
        return ret;

    /* ??? (m1n1) */
    rc.w32(0x4, 0);

    if ((ret = applyTunables(apcie, "apcie-common-tunables", rc, kTunableOptional)) != kIOReturnSuccess)
        return ret;

    /* Initialize PHY. */
    if ((ret = applyTunables(apcie, "apcie-phy-tunables", phy, kTunableOptional)) != kIOReturnSuccess)
        return ret;

    phy.set32(APCIE_PHY_CTRL, APCIE_PHY_CTRL_CLK0REQ);
    if (!poll32(phy, APCIE_PHY_CTRL, APCIE_PHY_CTRL_CLK0ACK, APCIE_PHY_CTRL_CLK0ACK, 50000, &value)) {
        LOG("phy: timeout enabling PHY CLK0 (ctrl 0x%08x)", value);
        return kIOReturnTimeout;
    }
    phy.set32(APCIE_PHY_CTRL, APCIE_PHY_CTRL_CLK1REQ);
    if (!poll32(phy, APCIE_PHY_CTRL, APCIE_PHY_CTRL_CLK1ACK, APCIE_PHY_CTRL_CLK1ACK, 50000, &value)) {
        LOG("phy: timeout enabling PHY CLK1 (ctrl 0x%08x)", value);
        return kIOReturnTimeout;
    }
    phy.clear32(APCIE_PHY_CTRL, APCIE_PHY_CTRL_RESET_T8103);
    IODelay(1);

    /* ??? (m1n1) */
    rc.set32(APCIE_PHYIF_CTRL, APCIE_PHYIF_CTRL_RUN);
    IODelay(1);

    /* Apply "fuses". */
    unsigned nfuses = 0;
    for (; pcie_fuse_bits_t8103[nfuses].width; nfuses++) {
        const struct fuse_bits *f = &pcie_fuse_bits_t8103[nfuses];
        UInt32 mask = (1u << f->width) - 1;
        UInt32 bits = (fuse.r32(f->src_reg) >> f->src_bit) & mask;
        phyIp.mask32(f->tgt_reg, mask << f->tgt_bit, bits << f->tgt_bit);
    }

    if ((ret = applyTunables(apcie, "apcie-phy-ip-pll-tunables", phyIp, kTunableRequired)) != kIOReturnSuccess)
        return ret;
    if ((ret = applyTunables(apcie, "apcie-phy-ip-auspma-tunables", phyIp, kTunableRequired)) != kIOReturnSuccess)
        return ret;

    LOG("phy: up, %u fuse fields applied; phy ctrl 0x%08x phyif ctl 0x%08x", nfuses, phy.r32(APCIE_PHY_CTRL),
        rc.r32(APCIE_PHYIF_CTRL));
    return kIOReturnSuccess;
}

/* m1n1 per-port loop, port 0, up to "Port failed to become idle" */
IOReturn Link::portInit()
{
    IOReturn ret;
    UInt32 value = 0;

    if ((ret = applyTunables(bridge, "apcie-config-tunables", port, kTunableRequired)) != kIOReturnSuccess)
        return ret;

    port.set32(APCIE_PORT_APPCLK, APCIE_PORT_APPCLK_EN);

    /* PERSTN */
    port.set32(APCIE_PORT_RESET, APCIE_PORT_RESET_DIS);

    if (!poll32(port, APCIE_PORT_STATUS, APCIE_PORT_STATUS_RUN, APCIE_PORT_STATUS_RUN, 250000, &value)) {
        LOG("port0: failed to come up (status 0x%08x)", value);
        return kIOReturnTimeout;
    }
    if (!poll32(port, APCIE_PORT_LINKSTS, APCIE_PORT_LINKSTS_BUSY, 0, 250000, &value)) {
        LOG("port0: failed to become idle (linksts 0x%08x)", value);
        return kIOReturnTimeout;
    }
    LOG("port0: ready; appclk 0x%08x status 0x%08x perst 0x%08x linksts 0x%08x", port.r32(APCIE_PORT_APPCLK),
        port.r32(APCIE_PORT_STATUS), port.r32(APCIE_PORT_RESET), value);
    return kIOReturnSuccess;
}

/* m1n1 per-port loop, port 0: DesignWare core tunables, link speed and width */
IOReturn Link::rootPortInit()
{
    IOReturn ret;
    UInt32 maxSpeed = 0;
    bool haveSpeed = false;

    /* Make Designware PCIe Core registers writable. */
    config.set32(DWC_DBI_RO_WR, DWC_DBI_RO_WR_EN);

    ret = applyTunables(bridge, "pcie-rc-tunables", config, kTunableRequired);
    if (ret == kIOReturnSuccess)
        ret = applyTunables(bridge, "pcie-rc-gen3-shadow-tunables", config, kTunableRequired);
    if (ret == kIOReturnSuccess)
        ret = applyTunables(bridge, "pcie-rc-gen4-shadow-tunables", config, kTunableRequired);

    if (ret == kIOReturnSuccess && propU32(bridge, "maximum-link-speed", &maxSpeed)) {
        haveSpeed = true;
        /*
         * m1n1: some devices override a maximum-link-speed of 1 in the first child node
         * ("target-link-speed" or "expected-link-speed").
         */
        if (maxSpeed == 1) {
            OSIterator *children = bridge->getChildIterator(gIODTPlane);
            IORegistryEntry *child = children ? OSDynamicCast(IORegistryEntry, children->getNextObject()) : NULL;
            UInt32 target = 0;
            if (propU32(child, "target-link-speed", &target) && (SInt32)target > 0)
                maxSpeed = target;
            else if (propU32(child, "expected-link-speed", &target) && (SInt32)target > 0)
                maxSpeed = target;
            OSSafeReleaseNULL(children);
        }
        if (maxSpeed == 0) {
            LOG("root port: invalid maximum-link-speed 0");
            ret = kIOReturnBadArgument;
        }
    }
    if (ret == kIOReturnSuccess && haveSpeed) {
        if (maxSpeed > 4)
            maxSpeed = 4;
        config.mask32(PCIE_CAP_BASE + PCIE_LNKCAP, PCIE_LNKCAP_SLS, maxSpeed);
        config.mask32(PCIE_CAP_BASE + PCIE_LNKCAP2, PCIE_LNKCAP2_SLS, ((1u << maxSpeed) - 1) << 1);
        config.mask16(PCIE_CAP_BASE + PCIE_LNKCTL2, PCIE_LNKCTL2_TLS, (UInt16)maxSpeed);
        config.set32(DWC_DBI_LINK_WIDTH_SPEED_CONTROL, DWC_DBI_SPEED_CHANGE);
    }
    if (ret == kIOReturnSuccess) {
        /* Max link width: one lane */
        config.mask32(DWC_DBI_PORT_LINK_CONTROL, DWC_DBI_PORT_LINK_MODE, DWC_DBI_PORT_LINK_MODE_1_LANE << 16);
        config.mask32(DWC_DBI_LINK_WIDTH_SPEED_CONTROL, DWC_DBI_LINK_WIDTH, 1u << 8);
        config.mask32(PCIE_CAP_BASE + PCIE_LNKCAP, PCIE_LNKCAP_MLW, 1u << 4);
    }

    /* Make Designware PCIe Core registers readonly (also when a step above failed). */
    config.clear32(DWC_DBI_RO_WR, DWC_DBI_RO_WR_EN);

    if (ret == kIOReturnSuccess)
        LOG("root port: max speed %u%s, width 1; lnkcap 0x%08x lnkctl2 0x%04x linksts 0x%08x", maxSpeed,
            haveSpeed ? "" : " (no maximum-link-speed, unchanged)", config.r32(PCIE_CAP_BASE + PCIE_LNKCAP),
            OSReadLittleInt16((volatile void *)config.va, PCIE_CAP_BASE + PCIE_LNKCTL2), port.r32(APCIE_PORT_LINKSTS));
    return ret;
}

/* apple_gpio_direction_output(); PERST# is active low */
void Link::gpioPerst(bool asserted)
{
    gpio.mask32(REG_GPIO(perstPin), REG_GPIOx_PERIPH | REG_GPIOx_MODE | REG_GPIOx_DATA,
                (REG_GPIOx_OUT << 1) | (asserted ? 0 : REG_GPIOx_DATA));
}

/*
 * proxy_linkup.py Port0.setup_link(), setup_port_tail() and train(): Linux
 * apple_pcie_setup_link() with the pwren GPIO replaced by the SMC key write gP0d
 * (SFAppleSMCRadioPowerOn; with sfapciesmc=0 the launcher must have powered the radio),
 * the rest of apple_pcie_setup_port(), and the LTSSM start.
 */
IOReturn Link::linkSequence()
{
    UInt32 value = 0;

    /* apple_pcie_setup_link() */
    port.set32(PORT_APPCLK, PORT_APPCLK_EN);

    /* Assert PERST# before setting up the clock */
    gpioPerst(true);

    /*
     * Linux: gpiod_set_value_cansleep(pwren, 1). On J293 pwren is SMC GPIO 13, key gP0d.
     * A failure is not fatal here: the launcher may have powered the radio already, and
     * the link result below tells. The 100 ms Tpvperl wait is done by the SMC code after
     * its write and again below after the reference clock setup.
     */
    if (smcOptions & kSFAppleSMCEnable) {
        bool wrote = false;
        LOG("link: PERST# asserted; powering the radio through the SMC (sfapciesmc=%u)", smcOptions);
        IOReturn smcRet = SFAppleSMCRadioPowerOn(smcOptions, &wrote);
        if (smcRet == kIOReturnSuccess)
            LOG("link: radio power is on (%s)", wrote ? "gP0d written" : "was already on, nothing written");
        else
            LOG("link: SMC radio power FAILED (0x%08x); going on, the link only trains if the radio is "
                "powered already",
                smcRet);
    } else {
        LOG("link: sfapciesmc=0, no SMC access; the launcher must have powered the radio");
    }

    /* apple_pcie_setup_refclk() */
    lane.set32(PHY_LANE_CTL, PHY_LANE_CTL_CFGACC);
    lane.set32(PHY_LANE_CFG, PHY_LANE_CFG_REFCLK0REQ);
    if (!poll32(lane, PHY_LANE_CFG, PHY_LANE_CFG_REFCLK0ACK, PHY_LANE_CFG_REFCLK0ACK, 50000, &value)) {
        LOG("refclk: REFCLK0 ack timeout (lane cfg 0x%08x)", value);
        return kIOReturnTimeout;
    }
    lane.set32(PHY_LANE_CFG, PHY_LANE_CFG_REFCLK1REQ);
    if (!poll32(lane, PHY_LANE_CFG, PHY_LANE_CFG_REFCLK1ACK, PHY_LANE_CFG_REFCLK1ACK, 50000, &value)) {
        LOG("refclk: REFCLK1 ack timeout (lane cfg 0x%08x)", value);
        return kIOReturnTimeout;
    }
    lane.clear32(PHY_LANE_CTL, PHY_LANE_CTL_CFGACC);
    lane.set32(PHY_LANE_CFG, PHY_LANE_CFG_REFCLKEN);
    port.set32(PORT_REFCLK, PORT_REFCLK_EN);

    /* Tpvperl, 100 ms; covers Tperst-clk too */
    IOSleep(100);

    /* Deassert PERST# */
    port.set32(PORT_PERST, PORT_PERST_OFF);
    gpioPerst(false);

    /* Wait for 100ms after PERST# deassertion (PCIe r5.0, 6.6.1) */
    IOSleep(100);

    if (!poll32(port, PORT_STATUS, PORT_STATUS_READY, PORT_STATUS_READY, 250000, &value)) {
        LOG("link: port0 ready wait timeout after PERST# release (status 0x%08x)", value);
        return kIOReturnTimeout;
    }
    LOG("link: refclk on, PERST# released, port0 ready; lane cfg 0x%08x refclk 0x%08x perst 0x%08x gpio%u 0x%08x",
        lane.r32(PHY_LANE_CFG), port.r32(PORT_REFCLK), port.r32(PORT_PERST), perstPin, gpio.r32(REG_GPIO(perstPin)));

    /*
     * Rest of apple_pcie_setup_port(). Linux clears REFCLK/APPCLK CGDIS here to allow
     * clock gating; under Darwin the link then drops after a few idle seconds. Keep the
     * clocks on, as proxy_linkup.py does.
     */
    port.set32(PORT_REFCLK, PORT_REFCLK_CGDIS);
    port.set32(PORT_APPCLK, PORT_APPCLK_CGDIS);

    /* Linux pinctrl (pcie_pins): CLKREQ# for port 0 is GPIO 150, function 1. */
    UInt32 clkreqBefore = gpio.r32(REG_GPIO(clkreqPin));
    gpio.mask32(REG_GPIO(clkreqPin), REG_GPIOx_PERIPH | REG_GPIOx_INPUT_ENABLE,
                (CLKREQ_FUNCTION << 5) | REG_GPIOx_INPUT_ENABLE);

    /* apple_pcie_port_setup_irq(): everything masked, MSI doorbell configured. */
    port.w32(PORT_INTMSK, 0xffffffff);
    port.w32(PORT_INTSTAT, 0xffffffff);
    port.w32(PORT_LINKCMDSTS, 0xffffffff);
    port.w32(PORT_MSIADDR, DOORBELL_ADDR);
    port.w32(PORT_MSIBASE, 0);
    port.w32(PORT_MSICFG, (MSI_NVECS_LOG2 << PORT_MSICFG_L2MSINUM_SHIFT) | PORT_MSICFG_EN);

    /* Reset all RID/SID mappings, and check for RAZ/WI registers */
    unsigned rid2sid = 0;
    for (; rid2sid < MAX_RID2SID; rid2sid++) {
        port.w32(PORT_RID2SID + 4 * rid2sid, 0xbad1d);
        if (port.r32(PORT_RID2SID + 4 * rid2sid) != 0xbad1d)
            break;
        port.w32(PORT_RID2SID + 4 * rid2sid, 0);
    }
    LOG("link: clocks kept ungated (appclk 0x%08x refclk 0x%08x), clkreq gpio%u 0x%08x->0x%08x, msi doorbell 0x%08x "
        "32 vectors (msicfg 0x%08x), %u rid2sid entries cleared",
        port.r32(PORT_APPCLK), port.r32(PORT_REFCLK), clkreqPin, clkreqBefore, gpio.r32(REG_GPIO(clkreqPin)),
        DOORBELL_ADDR, port.r32(PORT_MSICFG), rid2sid);

    /* train() */
    linksts = port.r32(PORT_LINKSTS);
    if (!(linksts & PORT_LINKSTS_UP)) {
        port.w32(PORT_LTSSMCTL, PORT_LTSSMCTL_START);
        bool up = poll32(port, PORT_LINKSTS, PORT_LINKSTS_UP, PORT_LINKSTS_UP, 500000, &linksts);
        if (!up) {
            LOG("link: port0 link did NOT come up within 500 ms; linksts 0x%08x intstat 0x%08x status 0x%08x", linksts,
                port.r32(PORT_INTSTAT), port.r32(PORT_STATUS));
            return kIOReturnTimeout;
        }
    }
    UInt16 lnksta = OSReadLittleInt16((volatile void *)config.va, PCIE_CAP_BASE + PCIE_LNKSTA);
    LOG("link: port0 link up; linksts 0x%08x, root port lnksta 0x%04x (speed %u, width %u)", linksts, lnksta,
        lnksta & 0xf, (lnksta >> 4) & 0x3f);
    return kIOReturnSuccess;
}

/*
 * Port 0 and PHY shutdown. The caller made sure the port block is readable (power domains
 * active, PHY running, port ready) and that nothing uses the device behind the port any
 * more. Each block gets its last access before it stops:
 *
 *   port  1. every port interrupt masked and acknowledged   (apple_pcie_port_teardown)
 *         2. LTSSM stopped                                   (apple_pcie_tunnel_stop)
 *   gpio  3. PERST# asserted: the device is in reset before its link and clocks go
 *   port  4. port reset asserted, APPCLK off, wait for not-ready  (m1n1 pcie_shutdown;
 *            the wait is Linux apple_pcie_tunnel_stop). Last port access. The status
 *            read comes after APPCLK and the port reset: the cold bring-up reads and
 *            writes the port block in exactly that state (portInit() applies the
 *            tunables and sets APPCLK_EN by read-modify-write before either is on).
 *   phy   5. reset bit, CLK1REQ, CLK0REQ cleared             (m1n1 pcie_shutdown, in its
 *            order and with its register writes). Last PHY access; the PHY control
 *            register itself stays readable, the bring-up reads it in this state.
 *
 * The lane reference clock requests and PORT_REFCLK are left as they are: neither source
 * turns them off, and the bring-up sets them again. The PMGR power domains are not
 * touched (m1n1 leaves them, and they have other users). This is the state m1n1 leaves
 * behind before the guest starts, from which the bring-up above is proven; the sequence
 * itself is UNTESTED on hardware.
 */
void Link::shutdown()
{
    UInt32 status = 0;

    port.w32(PORT_INTMSK, 0xffffffff);
    port.w32(PORT_INTSTAT, 0xffffffff);
    port.w32(PORT_LINKCMDSTS, 0xffffffff);
    port.clear32(PORT_LTSSMCTL, PORT_LTSSMCTL_START);
    LOG("shutdown: port0 interrupts masked, LTSSM stopped; linksts 0x%08x", port.r32(PORT_LINKSTS));

    gpioPerst(true);
    LOG("shutdown: PERST# asserted; gpio%u 0x%08x", perstPin, gpio.r32(REG_GPIO(perstPin)));

    port.clear32(PORT_PERST, PORT_PERST_OFF);
    port.clear32(PORT_APPCLK, PORT_APPCLK_EN);
    bool stopped = poll32(port, PORT_STATUS, PORT_STATUS_READY, 0, 100000, &status);
    LOG("shutdown: port0 in reset, APPCLK off; status 0x%08x%s", status, stopped ? "" : " (still ready after 100 ms)");

    phy.clear32(APCIE_PHY_CTRL, APCIE_PHY_CTRL_RESET_T8103);
    phy.clear32(APCIE_PHY_CTRL, APCIE_PHY_CTRL_CLK1REQ);
    phy.clear32(APCIE_PHY_CTRL, APCIE_PHY_CTRL_CLK0REQ);
    LOG("shutdown: PHY clock requests dropped; phy ctrl 0x%08x", phy.r32(APCIE_PHY_CTRL));
    linksts = 0;
}

} /* namespace */

IOReturn SFApcieT8103PortShutdown(IORegistryEntry *apcie, OSArray *regs, UInt32 flags, bool *portDown)
{
    Link state;
    Link *link = &state;
    IOReturn ret;
    bool pmgrActive = false;
    UInt32 smc = kSFAppleSMCEnable;

    if (portDown)
        *portDown = false;
    PE_parse_boot_argn("sfapciesmc", &smc, sizeof(smc));

    /* The same checks and the same order of first reads as the bring-up. */
    ret = link->setup(apcie, regs);
    if (ret == kIOReturnSuccess)
        ret = link->pmgr.clockGates(apcie, false, &pmgrActive);
    if (ret != kIOReturnSuccess) {
        LOG("shutdown: could not look at the port (0x%08x); nothing written", ret);
        return ret;
    }
    if (link->controllerLooksUp(pmgrActive)) {
        link->shutdown();
    } else {
        LOG("shutdown: the port is not running; no port or PHY register written");
    }
    if (portDown)
        *portDown = true;

    /*
     * Linux switches pwren off with PERST# asserted. THE VALUE WRITTEN HERE IS UNVERIFIED:
     * gP0d = 0x00800001 is proven to switch the radio on, 0x00800000 for off is the
     * obvious reverse and has never been written on hardware.
     */
    if (!(flags & kSFApcieShutdownRadioOff)) {
        LOG("shutdown: radio power left on (not asked to switch it off)");
    } else if (!(smc & kSFAppleSMCEnable)) {
        LOG("shutdown: sfapciesmc=0, no SMC access; radio power left on");
    } else {
        LOG("shutdown: switching the radio off through the SMC (gP0d = 0x%08x, UNVERIFIED value)",
            SF_SMC_RADIO_POWER_OFF);
        ret = SFAppleSMCRadioPowerOff(smc);
        if (ret == kIOReturnSuccess)
            LOG("shutdown: radio power off written");
        else
            LOG("shutdown: SMC radio power off FAILED (0x%08x); the port is down all the same", ret);
    }
    return ret;
}

IOReturn SFApcieT8103LinkBringUp(IORegistryEntry *apcie, OSArray *regs, UInt32 *linkstsOut)
{
    Link state; /* about 1 KiB */
    Link *link = &state;
    IOReturn ret;
    int mode = 1;
    bool pmgrActive = false;

    if (linkstsOut)
        *linkstsOut = 0;
    PE_parse_boot_argn("sfapcielink", &mode, sizeof(mode));
    {
        UInt32 smc = kSFAppleSMCEnable;
        PE_parse_boot_argn("sfapciesmc", &smc, sizeof(smc));
        link->smcOptions = smc;
    }

    ret = link->setup(apcie, regs);
    if (ret == kIOReturnSuccess) {
        ret = link->pmgr.clockGates(apcie, false, &pmgrActive);
        if (ret != kIOReturnSuccess)
            LOG("link: could not read the apcie power domains");
    }
    if (ret == kIOReturnSuccess) {
        link->pmgr.logSeen(pmgrActive ? "before (all active)" : "before");
        bool up = link->controllerLooksUp(pmgrActive);
        if (up && (link->linksts & PORT_LINKSTS_UP)) {
            LOG("link: port0 link is already up (linksts 0x%08x); nothing written", link->linksts);
        } else {
            if (up && mode != 2) {
                LOG("link: controller already initialised; running the port link sequence only");
            } else {
                LOG("link: running the whole controller, PHY and port bring-up%s", up ? " (sfapcielink=2)" : "");
                ret = link->controllerInit();
                if (ret == kIOReturnSuccess)
                    ret = link->portInit();
                if (ret == kIOReturnSuccess)
                    ret = link->rootPortInit();
            }
            if (ret == kIOReturnSuccess)
                ret = link->linkSequence();
        }
    }
    if (ret != kIOReturnSuccess)
        LOG("link: bring-up FAILED (0x%08x); hardware left as it is", ret);
    if (linkstsOut)
        *linkstsOut = link->linksts;
    return ret;
}
