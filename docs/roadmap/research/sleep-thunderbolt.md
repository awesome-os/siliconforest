<!-- Research input for the roadmap tickets; produced by a read-only research agent on 2026-11-09. Unverified on hardware. -->

# (A) Sleep/wake and power management — J293 / T8103

**1. How it works**

- **Linux on M1 implements s2idle only.** `drivers/cpuidle/cpuidle-apple.c` (177 lines, GPL-2.0-only OR MIT) has two states, both with `.enter_s2idle`: plain WFI and `apple_cpu_deep_wfi`, which sets bits 25:24 of `s3_5_c15_c5_0` (CYC_OVRD) around WFI. The SoC stays powered and SMC, ACIO and NVMe RTKit stay alive.
- **Apple deep sleep is not used by Linux.** `drivers/power/reset/macsmc-reboot.c` (298 lines) documents the SMC `MBSE` actions `slpw` and `susp` but only writes `offw`, `rest`, `off1` and `phra`.
- **Wake sources.** `drivers/input/misc/macsmc-input.c` (210 lines) handles the lid (`MSLD`) and power button (`bHLD`) via `pm_wakeup_dev_event`. `rtc-macsmc.c` (195 lines) has no alarm, so there is no RTC wake. Keyboard wake would be the SPI HID interrupt; I did not find a wake hook for it.
- **XNU's sleep path.** `IOPMrootDomain.cpp` (13,541 lines) has states OFF/RESTART/SLEEP/AOT/ON at lines 348-352. `sleepSystem()` (line 2992) notifies priority and ordinary clients, drives each driver's `setPowerState` through `IOServicePM.cpp` (9,622 lines), then `IOPlatformExpert::sleepKernel` calls `IOCPUSleepKernel()`.
- **T8103 builds with `USE_APPLEARMSMP`.** In `iokit/Kernel/arm/AppleARMSMP.cpp:478` the sequence is `IOPlatformActionsPreSleep`, `processor_sleep` for secondaries (`gPMGR->disableCPUCore`), `console_suspend`, then the boot CPU runs `PE_cpu_machine_quiesce`, `ml_arm_sleep()` (`osfmk/arm64/cpu.c:1061`) and `arm64_prepare_for_sleep(true)` (`machine_routines_asm.s:1709`), which ends in a non-returning WFI.
- **Wake re-enters at the kernel reset vector.** XNU's own comment at the `WITH_CLASSIC_S2R` block says newer SoCs wake via the "reconfig engine", not iBoot.

**2. Linux implementation and maturity on T8103**

All of these have T8103 code paths; I did not find a document stating sleep is qualified on J293.

| Piece | File | Lines | Notes |
|---|---|---|---|
| PCIe | `pcie-apple.c` | 3,093, GPL-2.0 | `apple_pcie_suspend_noirq` / `resume_noirq`, `s2idle_keep_link` param |
| DART | `apple-dart.c` | 2,576, GPL-2.0-only | `apple_dart_suspend` / `resume` |
| NVMe | `nvme/host/apple.c` | 2,675 | `apple_nvme_suspend` keeps the RTKit coprocessor running |
| Display | `drm/apple/apple_drv.c` | — | `apple_platform_suspend` (`drm_mode_config_helper_suspend`) |
| USB | `dwc3-apple.c` | 727 | `dwc3_apple_suspend` |
| PD controller | `tipd/core.c` | 2,314 | `cd321x_suspend_prepare`, `SSPS` power-state command |
| Thunderbolt | `thunderbolt/apple.c` | 2,437 | `router_sleep` off by default |
| PMGR | `pmgr-pwrstate.c` | 380, MIT-dual | power domains |

m1n1 contributes only `rtkit_sleep` / `rtkit_quiesce` and `dcp_shutdown(sleep)`.

**3. What PureDarwin has, and gaps**

- The full root domain and `IOServicePM` are present, as is user-side `IOPMLib.c` (`src/Libraries/IOKit/iokituser`).
- `pdpowerd` is 64 lines: it only listens for the ACPI power-button kernel event and halts. `PDACPIPower.cpp` (213 lines) is x86-only.
- `PDArmCPU::haltCPU` and `quiesceCPU` are empty stubs.
- The lab's `PDAppleBootCPUPMGR.cpp` (130 lines) has no idle states and panics on any secondary-core request.
- No lid or power-button source, no display blanking (boot framebuffer only, no DCP driver in the lab), no policy daemon, nothing that sets sleep-supported.
- Existing hooks, all marked UNTESTED in source: `PDBwfm::sleepWake` via `registerPrioritySleepWakeInterest` (`m1-bwfm/PDBwfm.cpp:281,710`) and the bridge resume path (`m1-apcie-host/PDApcieT8103.cpp:373-403`).

**4. Options and recommendation**

- **LinuxKPI port:** poor fit. Sleep is cross-cutting policy, not one driver, and the Linux PM core is not in `fbsdkpi`.
- **BSD (from memory, not read):** OpenBSD arm64 suspends Apple Silicon to an idle state, with `aplsmc` providing lid and power-button wake. It is ISC and a good reference for the SMC event keys. FreeBSD has nothing usable here.
- **Recommended: native IOKit, Linux as reference.** XNU already owns the state machine; the missing parts are platform glue.

Minimal first deliverable ("idle sleep"):
1. Lid and power-button events from SMC notifications, delivered through `receivePowerNotification(kIOPMClamshellClosed)`.
2. Backlight/display off, Wi-Fi down via the existing hook, CPU in WFI or deep WFI.
3. Wake on lid, power key or keyboard interrupt.
4. No `IOCPUSleepKernel`, i.e. a root-domain doze rather than SLEEP_STATE.

Real suspend (second ticket): SLEEP_STATE with driver `setPowerState`, secondary CPU off, PMGR domain gating, PCIe and DART restore. Apple deep sleep via `MBSE=susp` and reset-vector wake is research; Linux has no reference for it.

**5. Dependencies**

- **In the lab:** AIC (`PDAppleAIC.cpp`, 332 lines), the SMC/RTKit client (`PDAppleSMC.cpp`, 898 lines, key read/write), PMGR domain enables inside `PDApcieT8103Link.cpp`, and the DART pool. `AGENTS.md` still lists "SMC radio power from Darwin" as remaining; your brief says it works, so one of them is stale.
- **Missing:** SMC notification endpoint, a general PMGR service, SMP (the hypervisor removes cpu1-7), DCP or backlight control, a real `IOPMGR` with idle states, and a policy daemon.

**6. Work items, each with an on-hardware test**

| # | Item | Size | Acceptance test |
|---|---|---|---|
| A1 | SMC event service (`MSLD`, `bHLD`) | ~300 lines new | Serial log shows lid close/open and button press |
| A2 | Policy in `pdpowerd` | ~200 lines | Lid close calls the sleep API; button still halts |
| A3 | Idle sleep: quiesce Wi-Fi, blank panel, deep-WFI idle state in `IOPMGR` | ~400 lines | Lid close, 60 s, lid open: `en0` rejoins and pings in under 5 s; SMC battery drain reported |
| A4 | Driver `setPowerState` for bridge, DART, bwfm, SPI HID | ~500 lines | 20 sleep cycles without panic |
| A5 | PMGR service ported from `pmgr-pwrstate.c` | ~400 lines | Domain off/on verified by register readback |
| A6 | SMP and secondary shutdown | prerequisite ticket | Not sized |
| A7 | Deep-sleep research | unknown | — |

**Rig constraints**

- The hypervisor passes CYC_OVRD writes through but rejects any that set `CYC_OVRD_DISABLE_WFI_RET` (`src/hv/hv_exc.c:330-336`), so XNU's real sleep entry faults under it.
- A3 (WFI-based) is testable today.
- Real suspend needs one of: hypervisor emulation of the non-returning WFI plus re-entry at the guest reset vector (inferred design, similar to XNU's `test_sleep_in_vm`), or booting without the hypervisor, which loses the proxy console.

**Risks**

- Deep-WFI state retention on a single CPU under the hypervisor.
- The SMC notification format (it is in `macsmc.c`, 525 lines).
- RTKit coprocessors that never come back after sleep (m1n1 `dcp.c:277-285` notes such cases).
- The root domain's behaviour with no `powerd` acknowledging.

---

# (B) Thunderbolt / USB-C dock — J293 / T8103

**Rig fact.** Every run log shows the hypervisor removing index 0: `usb-drd0`, `dart-usb0`, `atc-phy0`, `acio0`, `acio-cpu0`, `dart-acio0`, `apciec0`, `dart-apciec0`, `atc0-dp*` and `hpm0` (`proxyclient/m1n1/hv/__init__.py:1624-1649`). The guest keeps port 1: `hpm1` (CD321x at I2C0 address 0x3f), `atc-phy1`, `usb-drd1` (dwc3 at 0x5_02280000, AIC 857), two USB DARTs, `acio1`, `apciec1` and the shared `dcpext`.

**1. The chain**

- **Connector to PD controller.** `apple,cd321x` on I2C0, level-low interrupt on pinctrl GPIO 106 for hpm0 (`t8103-jxxx.dtsi:72-76`).
- **PD controller driver.** `drivers/usb/typec/tipd/core.c` (2,314 lines, GPL-2.0) plus `tps6598x.h` (419) and `i2c.c` (106). It uses 4CC commands: `SSPS` (power state), `SWDF`/`SWUF` (data role), `SWSr`/`SWSk` (power role). `cd321x_read_data_status`, `cd321x_typec_update_mode` and `cd321x_tbt_notify` propagate USB3, DP and TBT/USB4 mode. The CD321x negotiates PD and alt modes itself; the OS only reads status.
- **PHY.** `drivers/phy/apple/atc.c` (3,412 lines, GPL-2.0 OR BSD-2-Clause) has modes OFF, USB2, USB3, USB3_DP, TBT, USB4 and DP. It handles the pipehandler mux, lane swap for orientation, and tunables plus fuses that m1n1 copies into the device tree (`src/kboot_atc.c`, 927 lines; `dt_set_acio_tunables` and `dt_set_pcie_tunables` in `kboot.c`).
- **USB host.** `dwc3-apple.c` (727 lines, GPL-2.0) is glue over the dwc3 core (20,654 lines in the directory). The core must be torn down and re-initialised on every plug. It sits behind two DARTs (`iommus = <&dwc3_1_dart_0 0>, <&dwc3_1_dart_1 1>`).
- **DisplayPort.** `dcpext` coprocessor, driven by `drivers/gpu/drm/apple` (14,868 lines of .c/.h; `dptxep.c` 878, `afk.c` 1,293) plus `drivers/mux/apple-display-crossbar.c` (923).
- **Thunderbolt/USB4.** `drivers/thunderbolt/apple.c` (2,437 lines, GPL-2.0) drives the ACIO Cortex-M3 over RTKit and the NHI, on top of the generic thunderbolt core (39,211 lines in the directory, including tests).
- **Tunnelled PCIe.** `apple,t8103-pciec` in `pcie-apple.c` (3,093 lines), with its own DARTs and hot-plug.

**2. Maturity on T8103**

- T8103 is the reference generation in this tree: `apple_cio_dp_is_t8103_style()`, `apple_atc_tunnel_is_t8103_style()`, `t8103_pciec_hw`.
- `t8103-jxxx.dtsi` wires both ports to dwc3, atcphy, ACIO and `dcpext`.
- This is Omacom-carried code. I did not verify what is in mainline; from memory, only tipd, atcphy and dwc3-apple are upstream.
- m1n1 runs first: `src/tps6598x.c` (540 lines; `tps6598x_powerup`, IRQ mask save/restore), `usb_phy_bringup`, `dapf.c` (239), and the tunables copy. The lab boots XNU from the Apple device tree, so tunables must be read from the ADT directly, as the lab's PCIe kext already does.

**3. What PureDarwin has, and gaps**

- **xHCI:** `RavynXHCIPort.cpp` (3,435 lines) already matches `snps,dwc3` and sets host mode in GCTL. It has hub enumeration with polled hot-plug, built-in bulk-only mass storage and HID, and MSI or device-tree interrupt.
  - It allocates DMA with `inTaskWithPhysicalMask` and has no `IOMapper`, so it cannot work behind a DART as is.
  - No isochronous transfers found by grep.
- **USB family:** `IOUSBFamily` (19,665 lines) and `PDUSBNCM` (780 lines).
- **Dock Ethernet:** no RTL8153, AX88179 or CDC-ECM driver found. Only NCM exists.
- **PCI hot-plug:** `IOPCIFamily` (21,247 lines) has the tunnel hot-plug logic (`kPCIHotPlugTunnel*`, `kIOPCITunnelLinkChangeKey`).
- **I2C:** `PDI2CFamily` (188 lines) with DesignWare and Qualcomm controllers only. No Apple (PASemi) I2C, no Apple pinctrl/GPIO interrupt driver, no Type-C framework.

**4. Options and recommendation**

- **LinuxKPI:** `fbsdkpi/pd/lkpi` is small and shaped for Wi-Fi and DRM. Linux's typec, phy, usb-role, mux and thunderbolt-bus frameworks are absent. Not worth it for stages 1-3.
- **BSD (from memory, not read):** OpenBSD has `tipd(4)`, `apliic`, `aplpinctrl` and dwc3-as-xhci attachment, all ISC. I recall no DP alt mode or Thunderbolt, and am unsure about a Type-C PHY driver.
- **Recommended:**
  - Stages 1-2: native IOKit kexts. Port `atc.c` (dual-licensed; keep BSD-2) and the cd321x subset of tipd (GPL-2.0, or an ISC rewrite from OpenBSD, roughly a quarter of the size).
  - Stage 4: port `thunderbolt/apple.c` and the needed core as GPL-2.0, as was done for PCIe.

**Staged delivery**

1. **USB 2.0 host on port 1.** PMGR `ATC1_USB`, atcphy USB2 mode, dwc3 host, DART support in the xHCI driver. Then USB3, with the atcphy pipehandler and orientation hard-coded or read once.
2. **PD and hot-plug.** I2C, the GPIO interrupt, a cd321x driver for plug, orientation, role and power status, and dwc3 re-initialisation per plug. Charging already works electrically because the CD321x and SMC handle it; this stage adds status.
3. **DP alt mode.** Needs a `dcpext` driver (RTKit, AFK, dptx, crossbar). This is the largest dependency.
4. **TBT/USB4.** ACIO, NHI, the connection manager, `apciec1` and its DARTs.

A typical USB-C dock (hub, Ethernet, HDMI) needs stages 1-2 plus a USB Ethernet driver for data, and stage 3 for video. A Thunderbolt dock gives nothing below stage 4 unless it falls back to USB (inferred: TB4 docks usually do, TB3 docks do not).

**5. Dependencies**

- **Exists:** AIC, the PMGR enable code, the DART pool (`pd_dart_pool.cpp`, fixed 16 MiB bounce pool, needs generalising to the USB DART instances), the RTKit client, the PCIe host code to extend for pciec, and tunables parsing.
- **Missing:** Apple I2C (`i2c-pasemi-core.c`, 465 lines), pinctrl/GPIO interrupts (`pinctrl-apple-gpio.c`, 619), the DCP stack, a generic RTKit service, and SPMI (not needed on J293; hpm is on I2C).

**6. Work items**

| # | Item | Size | Acceptance test |
|---|---|---|---|
| B1 | xHCI behind DART | ~400 lines | USB2 stick mounts on port 1 |
| B2 | atcphy port | ~2,000 lines | USB3 stick reports SuperSpeed, both orientations |
| B3 | Apple I2C and GPIO interrupt | ~900 lines | Read CD321x mode register returns `APP ` |
| B4 | cd321x driver and dwc3 re-plug | ~1,200 lines | 20 plug/unplug cycles; status in `ioreg` |
| B5 | USB Ethernet (RTL8153 or AX88179) | ~1,500 lines | DHCP and ping through a dock |
| B6 | `dcpext` DP | 8-10k lines | External monitor shows the console |
| B7 | ACIO/NHI and thunderbolt core | 10k+ lines | TB dock's PCIe xHCI/NIC enumerates |
| B8 | pciec hot-plug | ~1,000 lines | Unplug/replug re-enumerates |

**Rig constraints and risks**

- Only one port is free, so power and data share it. Test with a pass-through-power dock.
- The hypervisor hooks PMGR `ATC0_USB` (`hv/__init__.py:1489-1492`); the guest must not touch ATC0 domains.
- I did not verify which physical port is index 0.
- The dwc3 reset-per-plug behaviour.
- Whether `dcpext` firmware is loaded when booting through m1n1.
- The lab runs one CPU with polled xHCI events.