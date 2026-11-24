# M1 platform patch for PDArmPlatformExpert

Status: **source patch, not compiled, not booted.** It applies to PureDarwin
`e7aa00a60f0b56ef35aa24252e81467787bf71ea` and changes only
`src/Kernel/Extensions/PDArmPlatformExpert/`. The generic, QEMU, Pi, Sun50i and
SG2002 paths are unchanged. Kernel and board macros are out of scope here.

`platform.patch` SHA-256:
`c4a4e83a202177c7e91e5a0d9b187d657fa0f11fa8f0b554827fb8a2501a8c12`

## Why

The T8103 board configuration builds xnu with `USE_APPLEARMSMP=1`. In that
mode xnu's `cpu_boot_thread` (`iokit/Kernel/arm/AppleARMSMP.cpp`) registers the
processors itself. It waits for four things:

- the platform expert;
- a child interrupt controller under xnu's `PassthruInterruptController`;
- an `IOPMGR` service;
- the per-CPU device tree nubs.

Today the Apple platform expert instead builds `PDArmCPU`. That fails in
`PDArmGIC_map_cpu()`, because Apple SoCs have no GIC, and it would double-register
the processors anyway. The platform expert's `start()` therefore fails before
the AIC is ever brought up.

## What changes

1. **CPU ownership.** `PDArmPlatformExpert::start()` calls a new virtual,
   `startProcessors()`.
   - The base version is the original `PDArmCPU` loop, moved verbatim.
   - The Apple subclass waits up to 30 s for xnu's `PassthruInterruptController`
     to be the platform interrupt controller. It then publishes the power
     manager below and leaves the processors to xnu.
   - Without that controller, `start()` fails. There is no fallback to a GIC.
2. **AIC device tree name.** `PDAppleAIC` now also registers as
   `IOInterruptController%08X`, named from the AIC phandle (112 = `0x70` in the
   observed ADT).
   - Before, any ADT nub with `interrupt-parent` (uart0, for example) would
     sleep uninterruptibly in `lookUpInterruptController()`.
3. **AIC unmask.** After a handled event, `PDAppleAIC` unmasks the source again,
   or marks it hard-disabled if the handler soft-disabled it.
   - Reading `AIC_EVENT` masks the source in hardware (see Linux
     `irq-apple-aic.c`, `aic_irq_eoi`).
   - Before, each device IRQ fired once and then stayed masked.
4. **`PDAppleBootCPUPMGR`.** An `IOPMGR` subclass for the boot CPU only:
   - Idle states: none. xnu's `cpu_idle()` executes the WFI itself, and each
     callback disables xnu's idle timer.
   - Secondary core and cluster power requests panic with the reason; none
     returns success.
   - It touches no PMGR MMIO.

## Boot expectations (from source plus root's observed ADT facts)

- In the observed ADT, cpu0 has one interrupt specifier, so
  `register_aic_handlers()` registers nothing. No ADT edits are needed for
  CPU service.
- m1n1 HV removes the other seven CPUs from the ADT. `cpus=1` is still
  recommended, so that a fuller tree never reaches `enableCPUCore()`.
- The timer and IPIs need no driver here:
  - H13 `sleh_fiq` handles fast IPIs (`IPI_SR`) and the timer (`rtclock_intr`).
  - Under m1n1 HV, HCR ends up with `FMO=1` and `IMO=0`, by source reading;
    the runtime value was not observed.
    - C `hv_init` writes HCR without either bit. The proxyclient
      `HV.init()` then sets `FMO=1, IMO=0`
      (`proxyclient/m1n1/hv/__init__.py:1405-1416`) before `hv_start`.
    - `hv_start` only copies HCR for secondaries. Later C writes toggle only
      `VF` (`hv_update_fiq`) and `TTLB` (SPRR).
    - Physical FIQs therefore go to m1n1 at EL2. The guest's timer, IPI and
      PMC FIQs are injected separately as a virtual FIQ (`HCR_VF`).
    - AIC IRQs are taken by the guest at EL1, and the `/arm-io` MMIO
      (including the AIC) is mapped through unless traced.
- Console: m1n1 `hv_vuart` traps uart0 with TX and RX, which serves xnu's
  polled Samsung serial driver.

## Not covered

- Compile, link, kernel collection inclusion, load and boot. Host clang and the
  store's Darwin cross clang both stop in the built Kernel.framework headers
  (typed-allocator builtins), so the first compile is the real platform build.
- Reboot: on Apple SoCs, `PE_halt_restart` still points at the PSCI handler.
- CTRR: the observed ADT has no `/chosen/lock-regs`. Kernel owner decides.

## Check

```sh
python3 integrations/puredarwin/m1-bringup-platform/test_platform_patch.py -v
```

These are source checks against the pinned checkout:

- the patch applies to the pinned blobs;
- it touches only the expected paths;
- the generic loop is equivalent to the original;
- every `IOPMGR` virtual is overridden with its exact signature;
- the needed symbols are in `config/IOKit.exports`;
- eight mutants (success stub, armed idle timer, missing override, Apple
  `PDArmCPU`, untimed wait, changed generic loop, missing unmask, missing
  phandle registration) are each caught.
