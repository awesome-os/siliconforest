<!-- Research input for the roadmap tickets; produced by a read-only research agent on 2026-11-09. Unverified on hardware. -->

# SiliconForest on J293/T8103: SMP, cpufreq, release kernel, quiet boot, multi-user

Paths: `LAB`=`/home/chrisk/src/darwin-lab`, `PD`=`LAB/sources/puredarwin-m1-desktop`, `XNU`=`PD/src/Kernel/xnu`, `PE`=`PD/src/Kernel/Extensions/PDArmPlatformExpert`, `M1N1`=`LAB/sources/m1n1-baseline`, `LNX`=`/home/chrisk/source/aurora-linux`. Line counts in parentheses. **[I]** marks inference; everything else was read. Nothing was run on hardware.

## 1. Why one CPU, and what SMP needs

**Four layers each force one CPU today:**
- **Launcher:** `LAB/integrations/puredarwin/m1-firstboot/host_console.py:513` (677) sets `hv.smp, hv.wdt_cpu, hv.emulate_sprr = False, None, False` and `hv.start_secondary = stop_guest_control`, so any secondary start stops the guest.
- **Hypervisor:** with `smp=False`, `setup_adt()` (`M1N1/proxyclient/m1n1/hv/__init__.py:1670-1677`, 2009) deletes every `/cpus` node whose `state != "running"`.
- **Boot-args:** `cpus=1` in `BOOTARGS` (`LAB/integrations/puredarwin/m1-bringup/host_progress.py:115-116`, 1159); `ml_parse_cpu_topology` honours it (`XNU/osfmk/arm64/machine_routines.c:1182-1236`).
- **Platform code:** `PE/PDAppleBootCPUPMGR.cpp` (130) panics in `enableCPUCore`, `disableCPUCore` and `enable/disableCPUCluster`.

**How XNU starts secondaries** (`USE_APPLEARMSMP`, `XNU/pexpert/pexpert/arm64/board_config.h:170`; `MAX_CPUS 8`, `MAX_CPU_CLUSTERS 2`):
- `cpu_boot_thread` (`XNU/iokit/Kernel/arm/AppleARMSMP.cpp:155-234`, 554) waits for the platform expert, an AIC child controller and an `IOPMGR`, then calls `ml_processor_register` and `processor_boot` per CPU.
- `PE_cpu_start_internal` calls `gPMGR->enableCPUCore(cpu_id, 0)` (line 282). `IOPMGR.h:54` says RVBAR is programmed by iBoot on Apple Silicon.
- Topology comes from the ADT: `cpu-impl-reg`, `cluster-type`, `state` (`machine_routines.c:1255-1261`).
- IPIs are sysreg fast IPIs (`HAS_IPI`, `H13.h:39`). The CPU nodes have one interrupt specifier, so `aic_ipis` stays false and `ml_cpu_signal` is used. IPI and timer FIQs are handled in `sleh_fiq` (`LAB/integrations/puredarwin/m1-bringup-platform/README.md`).

**What exists:**
- `PE/PDAppleAIC.cpp` (332): IRQ dispatch and unmask; routes every source to CPU 0 (`write32(kAICTargetCPU(i), 1)`, line 176); has no IPI code and needs none.
- `PE/PDPerfControl.cpp` (85): E/P preferred-cluster policy, called from the base `startProcessors` (`PDArmPlatformExpert.cpp:107`). **[I]** the Apple override at line 624 does not call it.

**What is missing:** a real `enableCPUCore`, ported from `M1N1/src/smp.c:319-372` (986):
- PMGR `reg[0] + 0x54000` (`CPU_START_OFF_T8103`).
- `+0x4 = BIT(4*cluster+core)`; the m1n1 comment says IRQs do not work without it.
- `+0x8 + 4*cluster = BIT(core)`.
- RVBAR sits at `cpu-impl-reg+0`.
- **[I]** `cpu_idle` and the `CPU_PIO_CPU_STS` use (`XNU/osfmk/arm/cpu_common.c:700`) also need checking per CPU.

**m1n1 HV already supports multi-vCPU guests:**
- A write hook on `pmgr+0x54000..+0x20` (`cpustart_wh`, `__init__.py:1548-1576`) calls `start_secondary`, then `hv_start_secondary` (`M1N1/src/hv/hv.c:370`, 556).
- The loader pre-writes secondary RVBARs to `entry & ~0xfff` (`__init__.py:1810-1817`).
- vCPUs map 1:1 to physical cores.

**Cost under the HV:**
- `IPI_RR_LOCAL/GLOBAL` and `IPI_SR` trap (`M1N1/src/hv/hv_exc.c:297,364-381`, 662).
- Physical FIQs go to EL2 (`FMO=1`, `__init__.py:1413`) and are re-injected as vFIQ.
- `HV_TICK_RATE` is 1000 Hz (`hv.c:16`).
- `CYC_OVRD` writes with FIQ-mode or WFI-ret bits are refused (`hv_exc.c:330-337`).
- uart0 is trapped (vuart).
- Non-boot CPUs take a fast FIQ path; the slow path rendezvouses all CPUs.

## 2. CPU frequency

- **State today:** `cpufreq_init()` runs only on the Linux payload path (`M1N1/src/payload.c:464`) or via proxy `P_CPUFREQ_INIT` (`proxy.c:666`; `proxy.py:1245`). The lab launchers never call it (no `cpufreq` match in `LAB/tools` or `m1-bringup`). **[I]** XNU therefore runs at iBoot's P-state on one Icestorm core.
- **m1n1** (`M1N1/src/cpufreq.c`, 716): `t8103_clusters` (lines 305-309) are ECPU `0x210e00000` and PCPU `0x211e00000`, with APSC P-state 1 and defaults E=5, P=7. `set_pstate` (lines 81-141) writes `CLUSTER_PSTATE` (+0x20020) with `DESIRED1[4:0]`, `DESIRED2[15:12]` and `SET` (bit 25), then polls `BUSY` (bit 31). It also applies `cpu-apsc`, `ppt-thrtl`, `llc-thrtl`, `amx-thrtl` and PLL-relock features (lines 452-459).
- **Linux** (`LNX/drivers/cpufreq/apple-soc-cpufreq.c`, 461): the same register as `APPLE_DVFS_CMD` (0x20) at `0x210e20000` and `0x211e20000`; current P-state in `APPLE_DVFS_STATUS` (0x50) bits [7:4]; 400 µs timeout.
- **OPPs** (`LNX/arch/arm64/boot/dts/apple/t8103.dtsi:205-320`): E has 5 states, 600-2064 MHz. P has 600-3096 MHz and above; I read the table only up to its 3096 MHz entry.
- **Cheapest route:** one `p.cpufreq_init()` call in the launcher before `hv.start()` gives E at 2064 MHz and P at 1956 MHz. A native kext comes later, modelled on `PE/PDSun60iDVFS.cpp` (616).
- **P-core boot CPU:** not practical. XNU takes the `state="running"` node as boot CPU (`ml_is_boot_cpu`, `machine_routines.c:1105`), and m1n1 takes the CPU iBoot started (`smp.c:643`). Use SMP plus scheduler policy instead.

## 3. Release vs DEBUG

- **Selection:** `PD/nix/m1-bringup.nix` (14) sets `xnuKernelConfig = "DEBUG"` and `-DPUREDARWIN_ARM64_MACHINE_CONFIG=T8103`. That reaches `PUREDARWIN_XNU_KERNEL_CONFIG` (`PD/build.nix:221`) and `KERNEL_CONFIGS=` (`XNU/CMakeLists.txt:196,275`).
- **Hard gate:** `XNU/CMakeLists.txt:151-154` has `FATAL_ERROR "T8103 bringup requires DEBUG"`.
- **Config file:** `XNU/config/MASTER.arm64.t8103` defines RELEASE and DEVELOPMENT lines (106-107).
- **DEBUG dependencies in the lab:**
  - TrustCache placed in the post-kernel reservation; `arm_vm_init.c:1349-1353` rejects it outside DEBUG/DEVELOPMENT (`LAB/integrations/puredarwin/m1-bringup/CONTRACT.md:103-105`).
  - `/chosen/kernel-ctrr-to-be-enabled`, required by the DEBUG guest (`CONTRACT.md:217`; `host_progress.py:161-175`).
  - `debug=0x14e`, which depends on `/chosen/debug-enabled` (`XNU/pexpert/arm/pe_init.c:500-514`).
  - `-DDARWIN_LAB_M1_BRINGUP=1` (`XNU/cmake/MakeInc.def.in:76`), which undefines CTRR, PPL and PMAP_CS and adds progress breadcrumbs (`arm_init.c:351,621`; `m1_progress_entry.inc`, 40).
- **Recommendation:** go to DEVELOPMENT first. It keeps the TrustCache acceptance and the debug boot-args; RELEASE needs the TrustCache placement reworked. The kexts/KC variant set must match **[I]**.

## 4. Quiet boot and logo

- **Sources of text:**
  - `-v`, which makes `HV.set_bootargs` set `video.display=0` (`__init__.py:1679-1683`).
  - The launcher forcing `video.display = 2` (text) (`host_progress.py:138-147`).
  - `serial_video_mirror=1`, which calls `switch_to_video_console()` (`pe_init.c:588-593`).
  - `pdtextboot`, default 1, which keeps text after IOGraphics takes over (`XNU/osfmk/console/video_console.c:2769,2962-2967`).
  - The `-s` shell.
- **XNU graphics path:** `v_display==1` selects `kPEGraphicsMode` (`pe_init.c:574-581`), which sets `gc_graphics_boot` and paints no characters. The progress indicator is gated by `-noprogress` / `-progress` (`pe_init.c:325-341`).
- **m1n1:** `fb_init` and `fb_display_logo` run in `main.c:300-308`. `FB_SILENT_MODE` is in `config.h:9,33`. Built-in logos are `data/bootlogo_{48,128,256}`. A custom logo is a payload with magic `m1n1_logo_256128` (`Makefile:371-374`; `fb.c:514-520`, 573).
- **Needed:**
  - Build m1n1 with a SiliconForest logo.
  - Boot without `-v`, with `display=1` and `pdtextboot=0`.
  - Keep serial as the debug path (`serial=3` with no mirror).
  - Have the compositor take over the framebuffer without clearing it.
- **Power-on to desktop** also needs a non-proxy XNU loader. AGENTS.md lists "running outside the hypervisor" as remaining. **[I]** `M1N1/src` has no XNU kernel-collection loader; I only saw `chainload.c` and `boot_storage.c` on the kboot path.

## 5. Multi-user

- **Why single-user today:** `-s` sets `RB_SINGLE` (`XNU/bsd/kern/bsd_init.c:1229`); launchd runs `launchctl bootstrap -S System -s` and reaches later jobs only after the shell exits (`LAB/reports/sprint-009/rootfs-userspace-next-review.json`).
- **Harness assumptions:** it waits for the zsh prompt, sends `/bin/sh\r`, runs a nonce challenge, then one fixed command, `/usr/bin/darwin-desktop-test` (`host_progress.py:230-340,451`).
- **Already in the RAM root** (ext4 md0, 373 MB; `rootfs-plan.json`): `/sbin/launchd` and `/usr/libexec/pd-console-login`. The plist is `PD/src/Libraries/XPC/launchd/org.puredarwin.console-login.plist` (KeepAlive). `PD/image.nix:499-600` adds hostname, dbus and root-remount daemons.
- **Needed:**
  - Drop `-s`.
  - A LaunchDaemon for the session (compositor `chonkstep-darwin`, `pd-net-up`).
  - A harness mode that proves boot via a daemon-emitted nonce on serial.
- **Persistent root (inventory only):**
  - PureDarwin has `IONVMEFamily` (771), which is PCI NVMe.
  - Linux has `drivers/nvme/host/apple.c` (2675), `soc/apple/rtkit.c` (1201) and `sart.c` (403).
  - The lab has RTKit/SMC fragments only (`m1-apcie-host/PDAppleSMC.cpp`, `m1-gpu-rtkit-control`).
  - AGENTS.md forbids writes to internal storage.

## 6. Recommendations, work items, acceptance tests

| # | Item | Approach | Size | On-hardware acceptance |
|---|---|---|---|---|
| A | `cpufreq_init` in launcher | Configuration | ~5 lines | Fixed CPU loop runs ≥2x faster than baseline; `CLUSTER_PSTATE` readback E=5, P=7 |
| B | `PDAppleT8103PMGR::enableCPUCore` | Port from m1n1 `smp.c` (MIT) | 1 file, ~200 lines | Unit: PMGR write sequence matches `smp.c` |
| C | Launcher SMP: `hv.smp=True`, real `start_secondary`, drop `cpus=1`, relax the single-CPU inspector | Configuration plus harness | ~100 lines | `sysctl hw.ncpu` = 8; `hw.perflevel0/1` = 4+4 |
| D | Wire `pd_perfcontrol_start` on Apple; soak IPI/timer | Native | ~50 lines | 8-thread load for 10 min with no panic; UI threads land on P cluster |
| E | DEVELOPMENT kernel: lift cmake gate, nix attribute, re-verify the four DEBUG dependencies | Configuration plus small patches | ~5 files, <150 lines | Banner `DEVELOPMENT_ARM64_T8103`; desktop reached |
| F | RELEASE kernel | As E, plus TrustCache placement | Unknown | Banner `RELEASE_ARM64_T8103` |
| G | Quiet boot: `display=1`, no `-v`/mirror, `pdtextboot=0`, custom m1n1 logo, `debug` profile switch | Configuration plus m1n1 build | ~100 lines plus assets | Camera: logo to desktop with no glyphs; serial log still complete |
| H | Multi-user: session LaunchDaemon, harness nonce daemon | Native userspace plus harness | ~300 lines | `launchctl list` shows daemons; desktop starts with no typed input |
| I | Native DVFS kext | Port Linux `apple-soc-cpufreq.c` (GPL-2.0) | ~500 lines | P cluster at 3 GHz or more; no hang over 1000 transitions |

**Dependencies:**
- A is independent and should go first.
- B then C then D.
- E is independent, but SMP should be validated on DEBUG first because its assertions are useful.
- G needs H for a text-free desktop.
- F and standalone boot come after the rest.
- I comes after C.

**Risks and unknowns:**
- **Reset vector:** whether the lab-modified `start.s` still puts `LowResetVectorBase` at `entry & ~0xfff`, as the HV assumes.
- **Idle:** XNU's `CYC_OVRD`/WFI idle path against the HV refusal.
- **HV overhead:** trap cost of IPIs with 8 CPUs; the slow-path rendezvous.
- **Launcher inspector:** the framebuffer and stack inspector assumes "the single guest CPU is paused" (`host_progress.py:677-750`).
- **Thermal:** nothing was found for throttling once P-states are raised, beyond the m1n1 `*-thrtl` bits.
- **Scheduler:** E/P placement without Apple's performance controller.