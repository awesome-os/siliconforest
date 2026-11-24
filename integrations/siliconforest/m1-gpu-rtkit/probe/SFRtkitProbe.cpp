// SPDX-License-Identifier: GPL-2.0
/*
 * SFRtkitProbe: probe of the GPU coprocessor's ASC (/arm-io/gfx-asc) through the
 * mailbox/RTKit port in ../src. Matches IOResources, runs once at start.
 *
 *   sfrtkitprobe=0   nothing
 *   (default) / 1    READ-ONLY: device-tree description, one PMGR power-state read, and,
 *                    only if the GFX domain is powered, four ASC register reads
 *                    (CPU_CONTROL, CPU_STATUS, mailbox A2I_CONTROL and I2A_CONTROL). No
 *                    register is written, no interrupt is registered, no message is sent
 *                    or taken out of a FIFO (the FIFO data registers are never read).
 *   sfrtkitprobe=2   the same, then the RTKit boot of gfx-asc as Linux's asahi driver does
 *                    it up to "RTKit ready": set CPU_CONTROL.RUN (regs.rs start_cpu),
 *                    apple_rtkit_init (rtkit.rs RtKit::new), apple_rtkit_boot (gpu.rs
 *                    init). Stops there. See ../README.md for what this boot lacks and
 *                    why it is not expected to get all the way.
 *   sfrtkitprobe=3   DCP (SF-H item H1), nothing of the above: attaches to the display
 *                    coprocessor's ASC (/arm-io/dcp) as iBoot/m1n1 left it and wakes its
 *                    RTKit the way Linux's dcp_comp_bind() does, to log the endpoint map.
 *                    Lines start with "SFRtkitProbe: dcp". See runDcp() and ../README.md.
 *   sfrtkitprobe=4   DCP with its DART (SF-H item H2): as 3, and the firmware's buffer
 *                    requests are served through /arm-io/dart-dcp (sf_dcp_dart.cpp), so the
 *                    wake can complete. sfrtkitprobe_dart=1 stops the DART part after its
 *                    read-only stage (then behaves as 3); sfrtkitprobe_exit=1 quiesces the
 *                    coprocessor after a completed wake instead of leaving it awake.
 *   sfrtkitprobe=5   DCP through Linux's IOMMU/DMA API (../m1-dcp-dart): read-only look at
 *                    /arm-io/dart-disp0 streams 0 and 4 (the boot framebuffer must be found
 *                    mapped in stream 0), then the mode 4 wake with the pin's shmem_setup
 *                    code on that API, then one pass over the driver's other DMA calls on
 *                    the dcp stream. sfrtkitprobe_dvamask=1 hands out m1n1's address form.
 *   any other value  nothing
 *
 * Every line this kext logs starts with "SFRtkitProbe: ".
 */
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOService.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>

#include "sf_fbsd_os.h"
#include "sf_fbsd_xnu.h"

#include "sf_rtkit_platform.h"
#include "sf_rtkitprobe.h"
#include "sf_dcp_dart.h"
#include "sf_dart.h"

#define GFX_ASC "/arm-io/gfx-asc"
#define SGX     "/arm-io/sgx"
#define DCP     "/arm-io/dcp"

/*
 * T8103 PMGR power-state registers of the display coprocessor's domain and its parent:
 * Linux t8103-pmgr.dtsi ps_disp0_cpu0 (reg 0x10018) and ps_disp0_fe (reg 0x360) under
 * pmgr@23b700000; the same addresses come out of this machine's device tree for PMGR
 * devices DISP0_CPU0 (id 132) and DISP0_FE (id 84).
 */
#define T8103_PMGR_BASE       0x23b700000ULL
#define T8103_PMGR_DISP0_FE   0x360
#define T8103_PMGR_DISP0_CPU0 0x10018

/* m1n1 proxyclient/m1n1/hw/asc.py R_CPU_STATUS, R_MBOX_CTRL; Linux mailbox.c */
#define ASC_CPU_STATUS_RUNNING 0x01
#define ASC_CPU_STATUS_STOPPED 0x02
#define ASC_CPU_STATUS_IDLE    0x20
#define ASC_MBOX_A2I_CONTROL   0x110
#define ASC_MBOX_I2A_CONTROL   0x114
#define ASC_MBOX_CTRL_FULL     (1u << 16)
#define ASC_MBOX_CTRL_EMPTY    (1u << 17)
#define ASC_MBOX_CTRL_OVERFLOW (1u << 18)
#define ASC_MBOX_CTRL_COUNT(v) (((v) >> 20) & 0xf)

class SFRtkitProbe : public IOService
{
    OSDeclareDefaultStructors(SFRtkitProbe)

public:
    bool start(IOService *provider) override;

private:
    void run(int mode);
    void runDcp(int level);
    bool dispDartLook();
    bool dcpPowerState(UInt32 *fe, UInt32 *cpu0);
    void logRegisters(const char *when);
    bool firmwareMemoryOutsideKernel();
    bool regionOutsideKernel(const char *what, UInt64 base, UInt64 size);

    struct sf_rtkit_mbox_res res;
    volatile UInt32 *asc;  /* reg 0, first page: CPU control block */
    volatile UInt32 *mbox; /* reg 0 + 0x8000: mailbox control block */
    unsigned reads, writes;
};

OSDefineMetaClassAndStructors(SFRtkitProbe, IOService)

static void plog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void plog(const char *fmt, ...)
{
    char line[320];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sf_rtkit_platform_log(line);
}

static SInt32 gRan;

bool SFRtkitProbe::start(IOService *provider)
{
    if (!IOService::start(provider))
        return false;

    int mode = 1;
    PE_parse_boot_argn("sfrtkitprobe", &mode, sizeof(mode));
    if (mode <= 0) {
        IOLog("SFRtkitProbe: sfrtkitprobe=0, nothing done\n");
        return true;
    }
    /* once per boot, whatever IOKit does with this service */
    if (!OSCompareAndSwap(0, 1, &gRan))
        return true;
    if (sf_rtkit_platform_init(this) != 0) {
        IOLog("SFRtkitProbe: platform interface did not come up\n");
        return true;
    }
    sf_rtkit_platform_set_log_prefix("SFRtkitProbe: ");
    if (mode >= 3 && mode <= 5)
        runDcp(mode - 3);
    else if (mode <= 2)
        run(mode);
    else
        plog("sfrtkitprobe=%d is not a mode (0 off, 1 GPU read-only, 2 GPU RTKit boot, 3 DCP wake, 4 DCP wake with DART, 5 DCP through the IOMMU API): nothing done", mode);
    registerService();
    return true;
}

void SFRtkitProbe::logRegisters(const char *when)
{
    UInt32 control = asc[SF_RTKIT_ASC_CPU_CONTROL / 4];
    UInt32 status = asc[SF_RTKIT_ASC_CPU_STATUS / 4];
    UInt32 a2i = mbox[ASC_MBOX_A2I_CONTROL / 4];
    UInt32 i2a = mbox[ASC_MBOX_I2A_CONTROL / 4];
    reads += 4;

    plog("%s: ASC CPU_CONTROL 0x%08x (RUN %u) CPU_STATUS 0x%08x (%s%s%s)", when, control,
         (control & SF_RTKIT_ASC_CPU_CONTROL_RUN) ? 1 : 0, status,
         (status & ASC_CPU_STATUS_RUNNING) ? "running" : "not running", (status & ASC_CPU_STATUS_STOPPED) ? ", stopped" : "",
         (status & ASC_CPU_STATUS_IDLE) ? ", idle" : "");
    plog("%s: mailbox A2I_CONTROL 0x%08x (%s%s, count %u) I2A_CONTROL 0x%08x (%s%s, count %u)", when, a2i,
         (a2i & ASC_MBOX_CTRL_EMPTY) ? "empty" : (a2i & ASC_MBOX_CTRL_FULL) ? "full" : "not empty",
         (a2i & ASC_MBOX_CTRL_OVERFLOW) ? ", overflow" : "", (unsigned)ASC_MBOX_CTRL_COUNT(a2i), i2a,
         (i2a & ASC_MBOX_CTRL_EMPTY) ? "empty" : (i2a & ASC_MBOX_CTRL_FULL) ? "full" : "not empty",
         (i2a & ASC_MBOX_CTRL_OVERFLOW) ? ", overflow" : "", (unsigned)ASC_MBOX_CTRL_COUNT(i2a));
    plog("%s: coprocessor %s; AP->IOP FIFO %s, IOP->AP FIFO %s", when,
         (control & SF_RTKIT_ASC_CPU_CONTROL_RUN) ? "started (RUN set)" : "not started (RUN clear)",
         (a2i & ASC_MBOX_CTRL_EMPTY) ? "empty" : "holds messages", (i2a & ASC_MBOX_CTRL_EMPTY) ? "empty" : "holds messages");
}

bool SFRtkitProbe::regionOutsideKernel(const char *what, UInt64 base, UInt64 size)
{
    boot_args *args = (boot_args *)PE_state.bootArgs;
    UInt64 lo = args->physBase, hi = args->physBase + args->memSize;
    bool outside = size != 0 && (base + size <= lo || base >= hi);

    plog("firmware memory %s: 0x%llx len 0x%llx, %s kernel memory 0x%llx..0x%llx", what, (unsigned long long)base,
         (unsigned long long)size, outside ? "outside" : size ? "INSIDE" : "not described; cannot be shown outside",
         (unsigned long long)lo, (unsigned long long)hi);
    return outside;
}

static UInt64 dtU64(IORegistryEntry *node, const char *name)
{
    OSData *d = node ? OSDynamicCast(OSData, node->getProperty(name)) : NULL;
    UInt64 v = 0;

    if (d && d->getLength() >= 4)
        memcpy(&v, d->getBytesNoCopy(), d->getLength() >= 8 ? 8 : 4);
    return v;
}

/*
 * Once started the firmware runs from, and writes to, memory iBoot set aside for it: its
 * segments (gfx-asc segment-ranges) and the UAT handoff, page-table and shared regions (sgx
 * gfx-handoff-base, gpu-region-base, gfx-shared-region-base). Linux gets these as no-map
 * reserved memory from m1n1. Here the only thing that can be checked is that none of it lies
 * in the range this kernel manages; if any does, the coprocessor is not started.
 */
bool SFRtkitProbe::firmwareMemoryOutsideKernel()
{
    IORegistryEntry *sgx = IORegistryEntry::fromPath(SGX, gIODTPlane);
    IORegistryEntry *gfx = (IORegistryEntry *)res.node;
    bool ok = sgx != NULL;

    if (!sgx)
        plog("no " SGX " in the device tree: firmware memory regions unknown");
    if (sgx) {
        ok &= regionOutsideKernel("handoff (sgx gfx-handoff-base)", dtU64(sgx, "gfx-handoff-base"), dtU64(sgx, "gfx-handoff-size"));
        ok &= regionOutsideKernel("page tables (sgx gpu-region-base)", dtU64(sgx, "gpu-region-base"), dtU64(sgx, "gpu-region-size"));
        ok &= regionOutsideKernel("shared (sgx gfx-shared-region-base)", dtU64(sgx, "gfx-shared-region-base"),
                                  dtU64(sgx, "gfx-shared-region-size"));
        sgx->release();
    }
    /* segment-ranges: { u64 phys, u64 iova, u64 remap, u32 size, u32 flags } per segment (m1n1 adt.py) */
    OSData *seg = gfx ? OSDynamicCast(OSData, gfx->getProperty("segment-ranges")) : NULL;
    if (!seg || seg->getLength() < 32 || seg->getLength() % 32) {
        plog("gfx-asc segment-ranges missing or not 32-byte entries: firmware segments unknown");
        return false;
    }
    const UInt8 *p = (const UInt8 *)seg->getBytesNoCopy();
    for (unsigned off = 0; off < seg->getLength(); off += 32) {
        UInt64 phys;
        UInt32 size;
        memcpy(&phys, p + off, 8);
        memcpy(&size, p + off + 24, 4);
        ok &= regionOutsideKernel(off ? "segment __DATA (gfx-asc segment-ranges)" : "segment __TEXT (gfx-asc segment-ranges)", phys,
                                  size);
    }
    return ok;
}

void SFRtkitProbe::run(int mode)
{
    plog("start, mode %d (%s)", mode,
         mode == 1 ? "read-only: no register write, no interrupt, no message" : "RTKit boot attempt of " GFX_ASC);

    /* step 1: what the device tree says. No hardware access. */
    int ret = sf_rtkit_platform_mbox_resolve(GFX_ASC, &res);
    if (ret != 0) {
        plog(GFX_ASC " could not be resolved: %d. Stopping", ret);
        return;
    }
    plog("%s: iop,ascwrap-v4, reg 0 0x%llx len 0x%llx, mailbox (Linux %s) 0x%llx len 0x%llx", res.path,
         (unsigned long long)res.asc_phys, (unsigned long long)res.asc_size, res.linux_compatible,
         (unsigned long long)res.mbox_phys, (unsigned long long)res.mbox_size);
    plog("%s: AIC interrupts listed %u %u %u %u: send-empty %u, send-not-empty %u, recv-empty %u, recv-not-empty %u", res.path,
         (unsigned)res.aic_listed[0], (unsigned)res.aic_listed[1], (unsigned)res.aic_listed[2], (unsigned)res.aic_listed[3],
         (unsigned)res.aic_send_empty, (unsigned)res.aic_send_not_empty, (unsigned)res.aic_recv_empty,
         (unsigned)res.aic_recv_not_empty);

    /* step 2: is the block powered? One PMGR read. An unpowered register window is not touched. */
    uint32_t ps = 0;
    ret = sf_rtkit_platform_power_state(GFX_ASC, &ps);
    if (ret < 0) {
        plog("PMGR GFX power state could not be read (%d): ASC registers NOT read. Stopping", ret);
        return;
    }
    reads++;
    plog("PMGR GFX power state 0x%08x: target 0x%x actual 0x%x (%s)", ps, ps & 0xf, (ps >> 4) & 0xf,
         ret == 1 ? "powered" : "NOT powered");
    if (ret != 1) {
        plog("GFX domain is not powered: ASC registers NOT read. Stopping (%u register reads, 0 writes)", reads);
        return;
    }

    /* step 3: the ASC CPU control block and the mailbox control block, non-posted */
    asc = (volatile UInt32 *)sf_rtkit_platform_ioremap(res.asc_phys, 0x100);
    mbox = (volatile UInt32 *)sf_rtkit_platform_ioremap(res.mbox_phys, 0x200);
    if (!asc || !mbox) {
        plog("ASC registers could not be mapped. Stopping");
        return;
    }
    logRegisters("state");

    if (mode == 1) {
        plog("mode 1 done: %u register reads (1 PMGR, 4 ASC), 0 writes, 0 interrupts registered, 0 messages", reads);
        sf_rtkit_platform_iounmap((void *)asc);
        sf_rtkit_platform_iounmap((void *)mbox);
        asc = mbox = NULL;
        return;
    }

    /* ---- mode 2 ---- */
    if (!firmwareMemoryOutsideKernel()) {
        plog("firmware memory is not provably outside kernel memory: coprocessor NOT started. Stopping");
        return;
    }

    /* Linux regs.rs start_cpu(): asc_mmio.update(ASC_CPU_CONTROL, cpu_run = 1), before anything else */
    UInt32 control = asc[SF_RTKIT_ASC_CPU_CONTROL / 4];
    reads++;
    asc[SF_RTKIT_ASC_CPU_CONTROL / 4] = control | SF_RTKIT_ASC_CPU_CONTROL_RUN;
    OSSynchronizeIO();
    writes++;
    plog("step 3: CPU_CONTROL 0x%08x -> 0x%08x (RUN %s), as regs.rs start_cpu", control, control | SF_RTKIT_ASC_CPU_CONTROL_RUN,
         (control & SF_RTKIT_ASC_CPU_CONTROL_RUN) ? "was already set" : "set now");
    plog("not done here, done by Linux between start_cpu and RtKit::new: UAT handoff and page tables (mmu.rs Uat::new), initdata");

    sfos_xnu_init();
    if (sf_rtkitprobe_layer_up() != 0) {
        plog("LinuxKPI layer did not come up. Stopping (coprocessor left started)");
        return;
    }
    ret = sf_rtkitprobe_rtkit_boot();

    logRegisters("after");
    uint64_t taken = 0, handled = 0;
    sf_rtkit_platform_irq_stats(res.irq_recv_not_empty, &taken, &handled);
    plog("recv-not-empty (AIC %u): %llu interrupts taken, %llu handler runs", (unsigned)res.aic_recv_not_empty,
         (unsigned long long)taken, (unsigned long long)handled);
    if (res.irq_send_empty >= 0) {
        sf_rtkit_platform_irq_stats(res.irq_send_empty, &taken, &handled);
        plog("send-empty (AIC %u): %llu interrupts taken, %llu handler runs", (unsigned)res.aic_send_empty,
             (unsigned long long)taken, (unsigned long long)handled);
    }
    plog("mode 2 done: result %d; %u register reads and %u write by the probe itself, the rest by mailbox.c", ret, reads, writes);
    setProperty("SFRtkitProbeResult", ret == 0 ? "rtkit-ready" : "rtkit-boot-incomplete");
}

/* ---- DCP mode (sfrtkitprobe=3) ---- */

/*
 * Two PMGR reads, no write. Refuses anything but the SoC and PMGR window the two offsets
 * were taken from.
 */
bool SFRtkitProbe::dcpPowerState(UInt32 *fe, UInt32 *cpu0)
{
    IORegistryEntry *armio = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
    IORegistryEntry *pmgr = IORegistryEntry::fromPath("/arm-io/pmgr", gIODTPlane);
    OSData *compat = armio ? OSDynamicCast(OSData, armio->getProperty("compatible")) : NULL;
    bool t8103 = compat && compat->getLength() >= sizeof("arm-io,t8103") &&
                 !memcmp(compat->getBytesNoCopy(), "arm-io,t8103", sizeof("arm-io,t8103"));
    UInt64 base = 0;
    bool ok = false;

    if (t8103 && pmgr) {
        /* registry-owned array: used here, never released; this function runs once */
        OSArray *regs = IODTResolveAddressing(pmgr, "reg", NULL);
        IOMemoryDescriptor *d = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
        base = d ? d->getPhysicalSegment(0, NULL, kIOMemoryMapperNone) : 0;
    }
    if (base == T8103_PMGR_BASE) {
        volatile UInt32 *pfe = (volatile UInt32 *)sf_rtkit_platform_ioremap(T8103_PMGR_BASE + T8103_PMGR_DISP0_FE, 4);
        volatile UInt32 *pcpu = (volatile UInt32 *)sf_rtkit_platform_ioremap(T8103_PMGR_BASE + T8103_PMGR_DISP0_CPU0, 4);
        if (pfe && pcpu) {
            *fe = *pfe;
            *cpu0 = *pcpu;
            reads += 2;
            ok = true;
        }
        sf_rtkit_platform_iounmap((void *)pfe);
        sf_rtkit_platform_iounmap((void *)pcpu);
    } else {
        plog("dcp power: not T8103 or PMGR reg 0 is 0x%llx, expected 0x%llx", (unsigned long long)base,
             (unsigned long long)T8103_PMGR_BASE);
    }
    OSSafeReleaseNULL(armio);
    OSSafeReleaseNULL(pmgr);
    return ok;
}

/*
 * SF-H item H1. Stages, one "SFRtkitProbe: dcp <stage>:" line each:
 *
 *   start   what is about to happen
 *   dt      /arm-io/dcp from the device tree: ASC window, mailbox, AIC numbers. No access.
 *   power   PMGR DISP0_FE and DISP0_CPU0, read only. Stops unless DISP0_CPU0 is powered.
 *   regs    ASC CPU_CONTROL, CPU_STATUS, mailbox A2I/I2A control, read only. Stops unless
 *           RUN is set: this probe never starts the coprocessor (a stopped DCP needs its
 *           firmware mappings and a PMGR reset first, m1n1 dcp.c:211-219).
 *   cpu     Linux ORs RUN into CPU_CONTROL here (dcp.c:3979 at the pin); with RUN set that write
 *           changes nothing, so it is not made.
 *   rtkit-init, wake, rx, hello, endpoint, endpoint-map, buffer-request, power, endpoints,
 *   summary   probe.c sf_rtkitprobe_dcp_wake()
 *   regs    the four registers again
 *   irq     interrupt counters
 *   done    result
 *
 * Writes made: mailbox messages on the management endpoint only (IOP power state 0x220,
 * HELLO reply, endpoint-map replies, start of the system endpoints present below 0x20, AP
 * power state 0x20) and acknowledgments RTKit owes on syslog/ioreport/oslog. No register
 * of the display pipe, of either DART or of PMGR is written; no endpoint from 0x20 up is
 * started; the coprocessor is not shut down, quiesced or put to sleep afterwards.
 */
/*
 * Mode 5, read-only: the two streams of /arm-io/dart-disp0 the display driver uses. Stream 0
 * (the display-subsystem device) must hold iBoot's mapping of the boot framebuffer; stream 4
 * is the dcp's piodma child. Nothing of this DART is written here.
 */
bool SFRtkitProbe::dispDartLook()
{
    boot_args *args = (boot_args *)PE_state.bootArgs;
    uint64_t fb = args->Video.v_baseAddr & ~0x3fffULL;
    uint64_t fbSize = ((uint64_t)args->Video.v_rowBytes * args->Video.v_height + 0x3fff) & ~0x3fffULL;
    struct sf_dart_stream *s0 = sf_dart_open("/arm-io/dart-disp0", 0, 0);
    struct sf_dart_stream *s4 = sf_dart_open("/arm-io/dart-disp0", 0, 4);
    struct sf_dart_stream *sd = sf_dart_open("/arm-io/dart-dcp", 0, 0);
    uint64_t iova = 0;
    bool ok = s0 && s4 && sd;

    plog("dcp fb: boot framebuffer from the boot-args: phys 0x%llx, %lux%lu, %lu bytes per row, 0x%llx bytes",
         (unsigned long long)args->Video.v_baseAddr, (unsigned long)args->Video.v_width, (unsigned long)args->Video.v_height,
         (unsigned long)args->Video.v_rowBytes, (unsigned long long)fbSize);
    if (!ok) {
        plog("dcp fb: a display DART stream could not be opened (disp0/0 %s, disp0/4 %s, dcp/0 %s)", s0 ? "ok" : "FAILED",
             s4 ? "ok" : "FAILED", sd ? "ok" : "FAILED");
        return false;
    }
    if (sf_dart_find(s0, fb, fbSize, &iova) == 0) {
        uint64_t l1e = 0, pte = 0;
        uint64_t first = sf_dart_walk(s0, iova, &l1e, &pte), last = sf_dart_translate(s0, iova + fbSize - 0x4000);
        plog("dcp fb: dart-disp0 stream 0 maps it at iova 0x%llx, contiguous over 0x%llx bytes: first-level 0x%llx, leaf 0x%llx -> phys 0x%llx, last page -> 0x%llx: %s",
             (unsigned long long)iova, (unsigned long long)fbSize, (unsigned long long)l1e, (unsigned long long)pte,
             (unsigned long long)first, (unsigned long long)last,
             first == fb && last == fb + fbSize - 0x4000 ? "matches the boot framebuffer" : "DOES NOT MATCH");
        ok = first == fb && last == fb + fbSize - 0x4000;
    } else {
        plog("dcp fb: dart-disp0 stream 0 has NO contiguous mapping of the boot framebuffer");
        ok = false;
    }
    /* m1n1 kboot.c:2519: "boot framebuffer, mapped to dart-disp0 sid 0 and dart-dcp sid 0/5" */
    if (sf_dart_find(sd, fb, fbSize, &iova) == 0)
        plog("dcp fb: dart-dcp stream 0 maps it at iova 0x%llx", (unsigned long long)iova);
    else
        plog("dcp fb: dart-dcp stream 0 does not map it contiguously");
    if (sf_dart_find(s4, fb, 0x4000, &iova) == 0)
        plog("dcp fb: dart-disp0 stream 4 (piodma) maps its first page at iova 0x%llx", (unsigned long long)iova);
    else
        plog("dcp fb: dart-disp0 stream 4 (piodma) does not map it (m1n1 lists no framebuffer mapping for piodma either)");
    sf_dart_check(s0, "disp0 look");

    uint32_t l1w = 0, leafw = 0, leafc = 0, inv = 0, t;
    sf_dart_stats(s0, &l1w, &leafw, &leafc, &inv);
    sf_dart_stats(s4, &t, NULL, NULL, NULL);
    l1w += t;
    sf_dart_stats(s4, NULL, &t, NULL, NULL);
    leafw += t;
    sf_dart_stats(s4, NULL, NULL, NULL, &t);
    inv += t;
    plog("dcp fb: writes to dart-disp0 by this probe: %u first-level, %u leaf, %u cleared, %u invalidates (all must be 0)", l1w,
         leafw, leafc, inv);
    return ok && !l1w && !leafw && !leafc && !inv;
}

void SFRtkitProbe::runDcp(int level)
{
    const bool withDart = level >= 1;
    const bool viaIommu = level >= 2;
    plog("dcp start: attach to " DCP " as found, wake its RTKit as Linux dcp_comp_bind does, log the endpoint map, stop");

    int ret = sf_rtkit_platform_mbox_resolve(DCP, &res);
    if (ret != 0) {
        plog("dcp stop: " DCP " could not be resolved: %d", ret);
        return;
    }
    plog("dcp dt: %s iop,ascwrap-v4, reg 0 0x%llx len 0x%llx, mailbox 0x%llx len 0x%llx, AIC listed %u %u %u %u: send-empty %u, recv-not-empty %u",
         res.path, (unsigned long long)res.asc_phys, (unsigned long long)res.asc_size, (unsigned long long)res.mbox_phys,
         (unsigned long long)res.mbox_size, (unsigned)res.aic_listed[0], (unsigned)res.aic_listed[1],
         (unsigned)res.aic_listed[2], (unsigned)res.aic_listed[3], (unsigned)res.aic_send_empty,
         (unsigned)res.aic_recv_not_empty);

    UInt32 fe = 0, cpu0 = 0;
    if (!dcpPowerState(&fe, &cpu0)) {
        plog("dcp stop: PMGR power state not read; ASC registers NOT touched");
        return;
    }
    bool powered = ((cpu0 >> 4) & 0xf) == 0xf;
    plog("dcp power: DISP0_FE 0x%08x (target 0x%x actual 0x%x) DISP0_CPU0 0x%08x (target 0x%x actual 0x%x): %s", fe, fe & 0xf,
         (fe >> 4) & 0xf, cpu0, cpu0 & 0xf, (cpu0 >> 4) & 0xf, powered ? "powered" : "NOT powered");
    if (!powered) {
        plog("dcp stop: DISP0_CPU0 is not powered; ASC registers NOT touched (%u register reads, 0 writes)", reads);
        return;
    }

    asc = (volatile UInt32 *)sf_rtkit_platform_ioremap(res.asc_phys, 0x100);
    mbox = (volatile UInt32 *)sf_rtkit_platform_ioremap(res.mbox_phys, 0x200);
    if (!asc || !mbox) {
        plog("dcp stop: ASC registers could not be mapped");
        return;
    }
    logRegisters("dcp regs before");
    UInt32 control = asc[SF_RTKIT_ASC_CPU_CONTROL / 4];
    reads++;
    if (!(control & SF_RTKIT_ASC_CPU_CONTROL_RUN)) {
        plog("dcp stop: CPU_CONTROL.RUN is clear, the coprocessor is not running; this probe does not start it (%u register reads, 0 writes)",
             reads);
        return;
    }
    plog("dcp cpu: RUN is set; Linux's CPU_CONTROL |= RUN (dcp.c:3979) would write the value back unchanged, not written");

    /*
     * Mode 4 (H2). Read-only first: the DART's registers, its live root, and the walk of the
     * two addresses the firmware is known to place (crashlog, boot dc04; oslog, as m1n1 saw
     * it). Nothing is sent to the coprocessor unless all of that worked.
     */
    int serve = 0, quiesce = 0;
    if (viaIommu) {
        int exitMode = 0, dvaMask = 0;
        PE_parse_boot_argn("sfrtkitprobe_exit", &exitMode, sizeof(exitMode));
        PE_parse_boot_argn("sfrtkitprobe_dvamask", &dvaMask, sizeof(dvaMask));
        plog("dcp iommu: mode 5: display DARTs through Linux's IOMMU/DMA API (sfrtkitprobe_dvamask=%d, sfrtkitprobe_exit=%d)",
             dvaMask, exitMode);
        sf_dart_set_log_prefix("SFRtkitProbe: dcp ");
        sf_rtkitprobe_set_dva_mask(dvaMask);
        bool fbOk = dispDartLook();
        if (!fbOk)
            plog("dcp fb: the look at dart-disp0 did not come out as expected; going on, since nothing below touches dart-disp0");
        serve = 2;
        quiesce = exitMode == 1;
    } else if (withDart) {
        int dartStage = 2, exitMode = 0;
        PE_parse_boot_argn("sfrtkitprobe_dart", &dartStage, sizeof(dartStage));
        PE_parse_boot_argn("sfrtkitprobe_exit", &exitMode, sizeof(exitMode));
        plog("dcp dart: mode 4: serve buffer requests through /arm-io/dart-dcp (sfrtkitprobe_dart=%d: %s; sfrtkitprobe_exit=%d: %s); /arm-io/dart-disp0 is not touched",
             dartStage, dartStage >= 3 ? "read-only stage, then map and reply even if the self-check fails" :
                                         dartStage == 2 ? "read-only stage, then map and reply" : "read-only stage only, requests are not served",
             exitMode, exitMode == 1 ? "quiesce after a completed wake" : "leave awake");
        ret = sf_dcp_dart_open();
        if (ret != 0) {
            plog("dcp stop: the dcp DART is not usable (%d); nothing was sent to the coprocessor", ret);
            return;
        }
        int segmentsOk = sf_dcp_dart_selfcheck();
        sf_dcp_dart_walk("crashlog buffer (boot dc04)", 0xf00267000ULL);
        sf_dcp_dart_walk("oslog buffer (m1n1)", 0xf0025f000ULL);
        sf_dcp_dart_check("before wake");
        /* sfrtkitprobe_dart=3 goes on regardless, for a boot after this check itself is in doubt */
        if (segmentsOk == 0 && dartStage == 2) {
            plog("dcp stop: no firmware segment translates to the physical address the device tree gives, so this table walk cannot be trusted with a write; nothing was sent to the coprocessor");
            return;
        }
        serve = dartStage >= 2;
        quiesce = exitMode == 1;
    }

    sfos_xnu_init();
    if (sf_rtkitprobe_layer_up() != 0) {
        plog("dcp stop: LinuxKPI layer did not come up; nothing was sent");
        return;
    }
    ret = withDart ? sf_rtkitprobe_dcp_wake_serving(serve, quiesce) : sf_rtkitprobe_dcp_wake();

    logRegisters("dcp regs after");
    uint64_t taken = 0, handled = 0;
    sf_rtkit_platform_irq_stats(res.irq_recv_not_empty, &taken, &handled);
    uint64_t staken = 0, shandled = 0;
    if (res.irq_send_empty >= 0)
        sf_rtkit_platform_irq_stats(res.irq_send_empty, &staken, &shandled);
    plog("dcp irq: recv-not-empty (AIC %u) %llu taken, %llu handler runs; send-empty (AIC %u) %llu taken, %llu handler runs",
         (unsigned)res.aic_recv_not_empty, (unsigned long long)taken, (unsigned long long)handled, (unsigned)res.aic_send_empty,
         (unsigned long long)staken, (unsigned long long)shandled);
    if (!withDart) {
        plog("dcp done: result %d (%s); no endpoint from 0x20 up started, no shutdown, no sleep; RTKit stays attached and keeps acknowledging",
             ret, ret == 0 ? "RTKit awake, IOP and AP power states 0x20" : "wake did not complete");
        setProperty("SFRtkitProbeResult", ret == 0 ? "dcp-awake" : "dcp-wake-incomplete");
        return;
    }
    plog("dcp done: result %d (%s); no endpoint from 0x20 up started; %s", ret,
         ret != 0 ? "not completed, see the lines above" : quiesce ? "woken with every buffer request served, then quiesced" :
                                                                      "RTKit awake with every buffer request served",
         viaIommu ? "dart-disp0 read, never written" : "dart-disp0 never accessed");
    setProperty("SFRtkitProbeResult", ret == 0 ? (quiesce ? "dcp-served-quiesced" : "dcp-served-awake") : "dcp-h2-incomplete");
}
