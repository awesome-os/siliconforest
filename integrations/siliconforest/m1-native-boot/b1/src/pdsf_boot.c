/* SPDX-License-Identifier: MIT */
/*
 * SiliconForest stage, the m1n1 side: boot the SiliconForest XNU from payloads appended to
 * this m1n1 image, with no proxy and no controller.
 *
 * What the lab's launcher does over the USB proxy (host_progress.py run_worker with
 * native_boot.py, and m1n1's hv load_raw/setup_adt) is done here by m1n1 itself:
 * the MH_FILESET collection is loaded, SEPFW/preoslog/TrustCache are copied behind it, the
 * RAM disk is inflated to its place, the device tree is rewritten, the boot arguments are
 * built, the secondary cores are started and parked in the resident EL2 page
 * (stub/el2_drop.S), and m1n1's ordinary next-stage path vectors into that page, which
 * enters XNU at EL1.
 *
 * Nothing here touches storage: no NVMe, no NVRAM. Every address comes from the boot
 * arguments and the device tree m1n1 was given. (The installed stage 2 reads its records from
 * a file first: ../install/src/pdsf_file.c, not part of the bench's stage.)
 */

#include "pdsf.h"
#include "pdsf_root.h" /* PDSF_ROOT_UUID: written by build.sh; "" unless a disk root was named */
#include "adt.h"
#include "fb.h"
#include "heapblock.h"
#include "malloc.h"
#include "mcc.h"
#include "memory.h"
#include "sep.h"
#include "smp.h"
#include "soc.h"
#include "string.h"
#include "tinf/tinf.h"
#include "xnuboot.h"

extern u8 _pdsf_el2_start[], _pdsf_el2_end[];

/* stub/el2_drop.S */
#define EL2_PAGE        0x4000
#define EL2_BOOT_ENTRY  4
#define EL2_HOLD_ENTRY  8
#define EL2_PARAMS      0x3f00
#define EL2_HOLD_ACKS   0x3f40
#define EL2_FLAG_SPRR   1
#define EL2_FLAG_AMX    2
#define RVBAR_ADDR_MASK 0x00000ffffffff000ull

#define WDT_COUNT    0x10
#define WDT_ALARM    0x14
#define WDT_CTL      0x1c
#define WDT_RESET_EN 4
#define WDT_HZ       24000000u

#define ALIGN16K(x) ALIGN_UP((u64)(x), PDSF_PAGE)

#define fail(...)                                                                                  \
    do {                                                                                           \
        printf("PDSF: " __VA_ARGS__);                                                              \
        return -1;                                                                                 \
    } while (0)

/*
 * XNU's boot_args (xnu pexpert/pexpert/arm64/boot.h, the kernel this stage boots), by offset.
 * Everything the stage writes into the page goes through m1n1's struct boot_args, so each
 * member's offset there is checked against XNU's here; the command line, where the two
 * differ (m1n1 aligns its union of command lines to 8 bytes), is written by offset.
 */
#define XNU_BA_REVISION       0
#define XNU_BA_VERSION        2
#define XNU_BA_VIRT_BASE      8
#define XNU_BA_PHYS_BASE      16
#define XNU_BA_MEM_SIZE       24
#define XNU_BA_TOP_OF_KDATA   32
#define XNU_BA_VIDEO          40        /* six unsigned long: base, display, rowBytes, width, height, depth */
#define XNU_BA_MACHINE_TYPE   88
#define XNU_BA_DEVTREE        96
#define XNU_BA_DEVTREE_LENGTH 104
#define XNU_BA_COMMAND_LINE   108
_Static_assert(offsetof(struct boot_args, revision) == XNU_BA_REVISION, "boot_args.Revision");
_Static_assert(offsetof(struct boot_args, version) == XNU_BA_VERSION, "boot_args.Version");
_Static_assert(offsetof(struct boot_args, virt_base) == XNU_BA_VIRT_BASE, "boot_args.virtBase");
_Static_assert(offsetof(struct boot_args, phys_base) == XNU_BA_PHYS_BASE, "boot_args.physBase");
_Static_assert(offsetof(struct boot_args, mem_size) == XNU_BA_MEM_SIZE, "boot_args.memSize");
_Static_assert(offsetof(struct boot_args, top_of_kernel_data) == XNU_BA_TOP_OF_KDATA,
               "boot_args.topOfKernelData");
_Static_assert(offsetof(struct boot_args, video) == XNU_BA_VIDEO, "boot_args.Video");
_Static_assert(offsetof(struct boot_args, video.display) == XNU_BA_VIDEO + 8, "boot_args.Video.v_display");
_Static_assert(offsetof(struct boot_args, machine_type) == XNU_BA_MACHINE_TYPE, "boot_args.machineType");
_Static_assert(offsetof(struct boot_args, devtree) == XNU_BA_DEVTREE, "boot_args.deviceTreeP");
_Static_assert(offsetof(struct boot_args, devtree_size) == XNU_BA_DEVTREE_LENGTH,
               "boot_args.deviceTreeLength");
_Static_assert(sizeof(((struct boot_args *)0)->devtree_size) == 4, "boot_args.deviceTreeLength size");

static const char *const type_names[PDSF_TYPES] = {"end", "config", "kernel", "ramdisk"};

static int inflate_to(const struct pdsf_item *it, void *dest)
{
    unsigned int dlen = (unsigned int)it->raw, slen = (unsigned int)it->stored;
    int ret = tinf_gzip_uncompress(dest, &dlen, it->data, &slen);

    if (ret != TINF_OK || dlen != it->raw)
        fail("inflate failed (%d, %u of %lu bytes)\n", ret, dlen, it->raw);
    return 0;
}

static bool overlaps(u64 a, u64 alen, u64 b, u64 blen)
{
    return a < b + blen && b < a + alen;
}

static int get_range(int node, const char *name, u64 range[2], bool required)
{
    u32 len = 0;
    const void *p = adt_getprop(adt, node, name, &len);

    range[0] = range[1] = 0;
    if (!p)
        return required ? -1 : 0;
    if (len != 16)
        return -1;
    memcpy(range, p, 16);
    return 0;
}

static int hold_secondaries(u64 stub)
{
    int held = 0;

    if (smp_start_secondaries())
        printf("PDSF: not every secondary core started\n");
    smp_set_wfe_mode(true);
    for (int cpu = 0; cpu < MAX_CPUS; cpu++) {
        u64 reg, *ack, magic = 0x50444e4200000000ull | (u64)cpu;
        u64 deadline;

        if (cpu == boot_cpu_idx || !smp_is_alive(cpu))
            continue;
        if (cpu < 1 || cpu > 7)
            fail("unexpected secondary cpu %d\n", cpu);
        reg = smp_get_impl_reg(cpu);
        if (!reg)
            fail("cpu %d has no reset vector register\n", cpu);
        /* The kernel releases the core by writing its reset vector here (sfcpuheld=1). */
        write64(reg, stub);
        if ((read64(reg) & RVBAR_ADDR_MASK) != stub)
            fail("cpu %d: the reset vector register did not take the hold value\n", cpu);
        ack = (u64 *)(stub + EL2_HOLD_ACKS + 8 * (u64)cpu);
        if (smp_call4(cpu, (void *)(stub + EL2_HOLD_ENTRY), reg, stub, (u64)ack, magic))
            fail("cpu %d did not take the call into the EL2 page\n", cpu);
        deadline = timeout_calculate(3000000);
        while (read64((u64)ack) != magic) {
            if (timeout_expired(deadline))
                fail("cpu %d did not reach the EL2 page\n", cpu);
            sysop("dmb sy");
        }
        /* It never returns to m1n1: nothing is pending for the next-stage check. */
        smp_forget_call(cpu);
        held++;
    }
    printf("PDSF: %d secondary cores held in the EL2 page\n", held);
    return held;
}

static void wdt_arm(u64 base, u32 seconds)
{
    write32(base + WDT_CTL, 0);
    write32(base + WDT_COUNT, 0);
    write32(base + WDT_ALARM, seconds * WDT_HZ);
    write32(base + WDT_CTL, WDT_RESET_EN);
}

/*
 * 1: no SiliconForest payload after this image (m1n1 goes on as always);
 * 0: next_stage is set; < 0: a SiliconForest payload that cannot be booted.
 */
/*
 * `payload`: the record list, `limit` bytes readable there. `appended`: it lies behind this
 * image (the payload area); otherwise in m1n1's heap (a file the installed stage 2 read).
 */
int pdsf_boot_records(const u8 *payload, u64 limit, int appended)
{
    static struct pdsf_config cfg;
    static u8 seed[PDSF_SEED_BYTES];
    static char usb_paths[18][56];
    static const char *usb_remove[18];
    static const char *const usb_fmt[18] = {
        "/arm-io/dart-usb%d", "/arm-io/atc-phy%d", "/arm-io/usb-drd%d", "/arm-io/acio%d",
        "/arm-io/acio-cpu%d", "/arm-io/dart-acio%d", "/arm-io/apciec%d", "/arm-io/dart-apciec%d",
        "/arm-io/apciec%d-piodma", "/arm-io/i2c0/hpmBusManager/hpm%d",
        "/arm-io/i2c0/hpmBusManager1/hpm%d", "/arm-io/i2c3/hpmBusManager0/hpm%d",
        "/arm-io/nub-spmi-a0/hpm%d", "/arm-io/atc%d-dpxbar", "/arm-io/atc%d-dpphy",
        "/arm-io/atc%d-dpin0", "/arm-io/atc%d-dpin1", "/arm-io/atc-phy%d"};
    static char dfp_name[8];
    struct pdsf_set set;
    struct pdsf_macho macho;
    struct pdsf_layout_in in;
    struct pdsf_layout lay;
    const char *err = "";
    int root_kind = 0;
    u64 ram_end = cur_boot_args.phys_base + cur_boot_args.mem_size;
    const u8 *kc;
    int ret, cpus, smp;

    ret = pdsf_scan(payload, limit, &set);
    if (ret == 1)
        return 1;
    printf("\nPDSF: SiliconForest payload at %p%s\n", payload, appended ? "" : " (read from the boot partition)");
    if (ret)
        fail("damaged record list (%d)\n", ret);
    /* The heap starts at top_of_kernel_data: whoever loaded this image must have put that
     * after the payload, as m1n1's chainload and the lab's launcher do. */
    if (appended && (u64)set.end > cur_boot_args.top_of_kernel_data)
        fail("the payload reaches past top_of_kernel_data (0x%lx > 0x%lx)\n", (u64)set.end,
             cur_boot_args.top_of_kernel_data);
    if (chip_id != T8103)
        fail("this stage boots T8103 only (chip 0x%x)\n", chip_id);

    for (int t = 1; t < PDSF_TYPES; t++) {
        struct pdsf_item *it = &set.item[t];

        if (!it->present)
            continue;
        if (it->stored >> 32 || it->raw >> 32)
            fail("%s: larger than 4 GiB\n", type_names[t]);
        u32 crc = tinf_crc32(it->data, (unsigned int)it->stored);
        printf("PDSF: %-7s %lu bytes%s, %lu in memory, crc %08x %s\n", type_names[t], it->stored,
               (it->flags & PDSF_F_GZIP) ? " gzip" : "", it->raw, crc,
               crc == it->crc32 ? "ok" : "WRONG");
        if (crc != it->crc32)
            fail("%s: checksum differs\n", type_names[t]);
    }
    if (!set.item[PDSF_CONFIG].present || !set.item[PDSF_KERNEL].present)
        fail("config and kernel records are both needed\n");
    if (set.item[PDSF_CONFIG].flags & PDSF_F_GZIP)
        fail("the config record is plain text\n");
    if (pdsf_config_parse((const char *)set.item[PDSF_CONFIG].data, set.item[PDSF_CONFIG].stored,
                          &cfg, &err))
        fail("config: %s\n", err);
    printf("PDSF: bootargs: %s\n", cfg.bootargs);
    /* The root: the RAM root record of this image (rd=md0), or the one disk filesystem this
     * stage was built for (rd=uuid boot-uuid=<PDSF_ROOT_UUID>, pdsf_root.h, written by
     * build.sh). Any other command line would have the kernel wait for a root device and
     * then take its recovery path. */
    if (pdsf_root_check(cfg.bootargs, set.item[PDSF_RAMDISK].present, PDSF_ROOT_UUID, &root_kind, &err))
        fail("bootargs: %s\n", err);
    printf("PDSF: root: %s\n", root_kind == 1 ? "the RAM root of this image (rd=md0)" :
                                               "the disk filesystem this stage was built for (rd=uuid)");

    cpus = pdsf_cmdline_cpus(cfg.bootargs);
    if (cpus < 0)
        fail("bootargs: cpus= is 1..9\n");
    smp = cpus != 1;
    if (smp && !pdsf_cmdline_has(cfg.bootargs, "sfcpuheld=1"))
        fail("more than one processor needs sfcpuheld=1 in bootargs\n");

    /* The collection, inflated into m1n1's own heap when it travelled compressed */
    kc = set.item[PDSF_KERNEL].data;
    if (set.item[PDSF_KERNEL].flags & PDSF_F_GZIP) {
        void *scratch = heapblock_alloc_aligned(set.item[PDSF_KERNEL].raw, PDSF_PAGE);

        if (inflate_to(&set.item[PDSF_KERNEL], scratch))
            return -1;
        kc = scratch;
    }
    if (pdsf_macho_info(kc, set.item[PDSF_KERNEL].raw, &macho, &err))
        fail("kernel: %s\n", err);
    printf("PDSF: collection vm 0x%lx..0x%lx entry 0x%lx, %u segments\n", macho.vmin, macho.vmax,
           macho.entry, macho.segments);

    /* What the firmware left us */
    int mm = adt_path_offset(adt, "/chosen/memory-map");
    int chosen = adt_path_offset(adt, "/chosen");
    int defaults = adt_path_offset(adt, "/defaults");
    int uart_path[8];
    int uart = adt_path_offset_trace(adt, "/arm-io/uart0", uart_path);
    u64 sepfw[2], tc[2], preoslog[2], kmh[2], uart_base = 0;
    u32 uart_phandle = 0, adt_size = cur_boot_args.devtree_size;

    if (mm < 0 || chosen < 0 || defaults < 0 || uart < 0)
        fail("device tree lacks /chosen/memory-map, /defaults or /arm-io/uart0\n");
    if (get_range(mm, "SEPFW", sepfw, true) || get_range(mm, "TrustCache", tc, true) ||
        get_range(mm, "preoslog", preoslog, false) ||
        get_range(mm, "Kernel-mach_header", kmh, false))
        fail("unexpected /chosen/memory-map entries\n");
    if (adt_get_reg(adt, uart_path, "reg", 0, &uart_base, NULL) ||
        ADT_GETPROP(adt, uart, "AAPL,phandle", &uart_phandle) < 0)
        fail("uart0 has no address or phandle\n");
    if (pdsf_adt_size(adt, adt_size) < 0)
        fail("the device tree does not parse\n");
    for (int i = 0; i < 2; i++) {
        u64 *r = i ? tc : sepfw;

        if (!r[1] || r[0] < cur_boot_args.phys_base || r[1] > ram_end - r[0])
            fail("SEPFW or TrustCache outside RAM\n");
    }
    if (preoslog[1] && (preoslog[0] < cur_boot_args.phys_base || preoslog[1] > ram_end - preoslog[0]))
        fail("preoslog outside RAM\n");

    /* The resident EL2 page, in m1n1's memory: below everything XNU is given */
    size_t el2_size = _pdsf_el2_end - _pdsf_el2_start;
    u8 *el2 = memalign(EL2_PAGE, EL2_PAGE);
    u64 params[4];

    if (el2_size != EL2_PAGE || memcmp(_pdsf_el2_start + EL2_PARAMS, "PDNB1", 5) || !el2)
        fail("no EL2 page\n");
    memcpy(el2, _pdsf_el2_start, EL2_PAGE);
    params[0] = 0x31424e4450ull;
    params[1] = uart_base;
    params[2] = (cfg.sprr ? EL2_FLAG_SPRR : 0) | (cfg.amx ? EL2_FLAG_AMX : 0);
    params[3] = 0;
    memcpy(el2 + EL2_PARAMS, params, sizeof(params));
    dc_cvau_range(el2, EL2_PAGE);
    ic_ivau_range(el2, EL2_PAGE);

    /* Layout: everything m1n1 has allocated so far stays below phys_base */
    memset(&in, 0, sizeof(in));
    in.stage_top = max((u64)heapblock_high_water(), (u64)set.end);
    in.reserve = (u64)cfg.reserve_mb << 20;
    in.fixed_base = cfg.xnu_base;
    in.ram_end = ram_end;
    in.image_size = macho.vmax - macho.vmin;
    in.devtree_size = adt_size;
    /* The launcher's capacity (the tree's size, rounded up) unless the properties added
     * below, about 430 bytes, would not fit in it. */
    in.adt_capacity = ALIGN16K(adt_size + 1024);
    in.sepfw_size = sepfw[1];
    in.preoslog_size = preoslog[1];
    in.tc_size = tc[1];
    in.ramdisk_size = set.item[PDSF_RAMDISK].present ? set.item[PDSF_RAMDISK].raw : 0;
    ret = pdsf_layout(&in, &lay);
    if (ret)
        fail("%s (%d)\n",
             ret == -3 ? "xnu-base lies in memory the stage itself uses"
                       : "no room for the kernel and its payloads",
             ret);
    if ((u64)el2 + EL2_PAGE > lay.phys_base || (u64)el2 < cur_boot_args.phys_base)
        fail("the EL2 page is not below XNU's memory\n");
    for (size_t i = 0; i < mcc_carveout_count; i++)
        if (overlaps(lay.phys_base, lay.top - lay.phys_base, mcc_carveouts[i].base,
                     mcc_carveouts[i].size))
            fail("the kernel's place intersects a memory controller carve-out\n");
    if (overlaps(lay.phys_base, lay.top - lay.phys_base, sepfw[0], sepfw[1]) ||
        overlaps(lay.phys_base, lay.top - lay.phys_base, tc[0], tc[1]) ||
        overlaps(lay.phys_base, lay.top - lay.phys_base, (u64)adt, adt_size))
        fail("the kernel's place intersects firmware data still to be copied\n");
    heapblock_set_limit((void *)lay.phys_base);
    printf("PDSF: XNU memory 0x%lx..0x%lx; kernel 0x%lx, bootargs 0x%lx, adt 0x%lx,\n"
           "PDSF: trustcache 0x%lx, ramdisk 0x%lx, top 0x%lx; EL2 page %p\n",
           lay.phys_base, ram_end, lay.guest_base, lay.bootargs, lay.adt, lay.tc, lay.ramdisk,
           lay.top, el2);

    /* Load */
    pdsf_macho_load(kc, set.item[PDSF_KERNEL].raw, &macho, (u8 *)lay.guest_base);
    memset((void *)lay.sepfw, 0, lay.adt - lay.sepfw);
    memcpy((void *)lay.sepfw, (void *)sepfw[0], sepfw[1]);
    if (preoslog[1])
        memcpy((void *)lay.preoslog, (void *)preoslog[0], preoslog[1]);
    memset((void *)lay.adt, 0, lay.ramdisk - lay.adt);
    memcpy((void *)lay.tc, (void *)tc[0], tc[1]);
    if (set.item[PDSF_RAMDISK].present) {
        const struct pdsf_item *rd = &set.item[PDSF_RAMDISK];

        if (rd->flags & PDSF_F_GZIP) {
            printf("PDSF: inflating the RAM disk...\n");
            if (inflate_to(rd, (void *)lay.ramdisk))
                return -1;
        } else {
            memcpy((void *)lay.ramdisk, rd->data, rd->raw);
        }
        memset((void *)(lay.ramdisk + rd->raw), 0, lay.top - lay.ramdisk - rd->raw);
    }

    /* Entropy: the kernel's 256-byte boot seed, from the Secure Enclave's ROM generator */
    if (cfg.have_seed) {
        memcpy(seed, cfg.seed, sizeof(seed));
        printf("PDSF: boot seed from the configuration (bench only)\n");
    } else if (sep_get_random(seed, sizeof(seed)) != sizeof(seed)) {
        fail("the SEP gave no boot seed\n");
    } else {
        printf("PDSF: boot seed from the SEP\n");
    }

    /* Device tree */
    static u32 zero32;
    u64 r_sepfw[2] = {lay.sepfw, sepfw[1]}, r_tc[2] = {lay.tc, tc[1]};
    u64 r_adt[2] = {lay.adt, in.adt_capacity}, r_ba[2] = {lay.bootargs, PDSF_PAGE};
    u64 r_log[2] = {lay.preoslog, preoslog[1]}, r_kmh[2] = {lay.guest_base, 0};
    u64 r_rd[2] = {lay.ramdisk, in.ramdisk_size};
    struct pdsf_adt_set sets[12];
    struct pdsf_adt_rules rules;
    u32 n = 0;
    const char *map = "/chosen/memory-map";

    memset(&rules, 0, sizeof(rules));
    sets[n++] = (struct pdsf_adt_set){"/chosen", "random-seed", seed, sizeof(seed), 0, 0};
    sets[n++] =
        (struct pdsf_adt_set){"/chosen", "kernel-ctrr-to-be-enabled", &zero32, 4, 0, 0};
    sets[n++] = (struct pdsf_adt_set){map, "SEPFW", r_sepfw, 16, 1, 0};
    sets[n++] = (struct pdsf_adt_set){map, "TrustCache", r_tc, 16, 1, 0};
    sets[n++] = (struct pdsf_adt_set){map, "DeviceTree", r_adt, 16, 0, 0};
    sets[n++] = (struct pdsf_adt_set){map, "BootArgs", r_ba, 16, 0, 0};
    sets[n++] = (struct pdsf_adt_set){map, "preoslog", r_log, 16, 1, 0};
    sets[n++] = (struct pdsf_adt_set){map, "Kernel-mach_header", r_kmh, 16, 1, 0};
    if (in.ramdisk_size)
        sets[n++] = (struct pdsf_adt_set){map, "RAMDisk", r_rd, 16, 0, 0};
    if (cfg.serial)
        sets[n++] = (struct pdsf_adt_set){"/defaults", "serial-device", &uart_phandle, 4, 0, 0};
    rules.set = sets;
    rules.nset = n;
    rules.prune_cpus = !smp;
    rules.strip_oslog = 1;
    if (cfg.usb_remove != PDSF_USB_KEEP) {
        for (int i = 0; i < 18; i++) {
            snprintf(usb_paths[i], sizeof(usb_paths[i]), usb_fmt[i], cfg.usb_remove);
            usb_remove[i] = usb_paths[i];
        }
        snprintf(dfp_name, sizeof(dfp_name), "atc%d", cfg.usb_remove);
        rules.remove = usb_remove;
        rules.nremove = 18;
        rules.dfp_remove = dfp_name;
    }
    long adt_len = pdsf_adt_rebuild(adt, adt_size, (u8 *)lay.adt, in.adt_capacity, &rules);
    if (adt_len <= 0)
        fail("device tree rewrite failed (%ld)\n", adt_len);
    printf("PDSF: device tree %u -> %ld bytes; %u nodes removed, __OS_LOG left %u nodes\n",
           adt_size, adt_len, rules.removed, rules.oslog_stripped);

    /* Boot arguments */
    struct boot_args *ba = (struct boot_args *)lay.bootargs;
    char *cmdline;
    size_t cmdmax;

    *ba = cur_boot_args;
    switch (ba->revision) {
        case 1:
            cmdline = ba->rv1.cmdline;
            cmdmax = CMDLINE_LENGTH_RV1;
            break;
        case 2:
            cmdline = ba->rv2.cmdline;
            cmdmax = CMDLINE_LENGTH_RV2;
            break;
        case 3:
            cmdline = ba->rv3.cmdline;
            cmdmax = CMDLINE_LENGTH_RV3;
            break;
        default:
            fail("boot arguments revision %u\n", ba->revision);
    }
    if (strlen(cfg.bootargs) >= cmdmax)
        fail("bootargs do not fit revision %u\n", ba->revision);
    /*
     * XNU's boot_args has the command line directly after deviceTreeLength. m1n1's struct
     * (xnuboot.h) puts its union of command lines on an 8-byte boundary, four bytes later:
     * written through that member, the kernel sees a command line that starts with four
     * NUL bytes, i.e. none (boots sfb01..sfb03: no serial console, no rd=md0). The flags
     * after the command line are at the same offset in both.
     */
    (void)cmdline;
    cmdline = (char *)ba + XNU_BA_COMMAND_LINE;
    memset(cmdline, 0, cmdmax);
    memcpy(cmdline, cfg.bootargs, strlen(cfg.bootargs));
    ba->phys_base = lay.phys_base;
    ba->virt_base = lay.virt_base;
    ba->mem_size = lay.mem_size;
    ba->top_of_kernel_data = lay.top;
    ba->devtree = (void *)(lay.adt - lay.phys_base + lay.virt_base);
    ba->devtree_size = (u32)adt_len;
    ba->video.display = cfg.display;
    /*
     * The page as the kernel will read it: the command line at XNU's offset must be the one
     * asked for, and the root it names is checked once more on these bytes (the RAM root of
     * this image, or the disk filesystem this stage was built for; nothing else).
     */
    {
        const char *seen = (const char *)lay.bootargs + XNU_BA_COMMAND_LINE;
        int seen_kind = 0;

        if (strlen(seen) != strlen(cfg.bootargs) || memcmp(seen, cfg.bootargs, strlen(cfg.bootargs)) ||
            pdsf_root_check(seen, set.item[PDSF_RAMDISK].present, PDSF_ROOT_UUID, &seen_kind, &err) ||
            seen_kind != root_kind)
            fail("the boot arguments page does not carry the command line with its root where XNU reads it\n");
        printf("PDSF: command line as XNU reads it (offset %d): %s\n", XNU_BA_COMMAND_LINE, seen);
    }

    dc_cvau_range((void *)lay.guest_base, lay.top - lay.guest_base);
    ic_ivau_range((void *)lay.guest_base, ALIGN16K(in.image_size));

    /* Processors */
    if (smp && hold_secondaries((u64)el2) < 0)
        return -1;

    u64 entry = lay.guest_base + (macho.entry - macho.vmin);

    if (cfg.wdt) {
        int wdt_path[8];
        u64 wdt_base = 0;

        if (adt_path_offset_trace(adt, "/arm-io/wdt", wdt_path) < 0 ||
            adt_get_reg(adt, wdt_path, "reg", 0, &wdt_base, NULL))
            fail("no watchdog to arm\n");
        wdt_arm(wdt_base, cfg.wdt);
        printf("PDSF: SoC watchdog armed, reset in %u s\n", cfg.wdt);
    }

    /*
     * Diagnostic (boot argument pdsfstop=1): everything is in place and the secondary cores
     * are held, but XNU is not entered. m1n1 goes on to its USB proxy, where the launcher
     * reads back and compares what was placed (b1/native_stage.py, word stagestop).
     */
    if (pdsf_cmdline_has(cfg.bootargs, "pdsfstop=1")) {
        printf("PDSF: stop requested: not entering XNU at 0x%lx; returning to the proxy\n",
               lay.guest_base + (macho.entry - macho.vmin));
        return -1;
    }

    fb_improve_logo();
    printf("PDSF: entering XNU at 0x%lx (EL1 through the EL2 page), boot arguments 0x%lx\n",
           entry, lay.bootargs);
    printf("PDSF: code at the entry, as m1n1 reads it: %016lx %016lx\n", read64(entry),
           read64(entry + 8));
    next_stage.entry = (generic_func *)(el2 + EL2_BOOT_ENTRY);
    next_stage.args[0] = entry;
    next_stage.args[1] = lay.bootargs;
    next_stage.args[2] = next_stage.args[3] = next_stage.args[4] = 0;
    next_stage.restore_logo = true;
    return 0;
}

int pdsf_boot(void *payload)
{
    u64 ram_end = cur_boot_args.phys_base + cur_boot_args.mem_size;

    if ((u64)payload < cur_boot_args.phys_base || (u64)payload >= ram_end)
        return 1;
    return pdsf_boot_records(payload, ram_end - (u64)payload, 1);
}
