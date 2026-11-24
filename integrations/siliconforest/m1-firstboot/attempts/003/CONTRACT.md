# T8103 XNU first-entry console experiment, revision 3

This candidate enters the real XNU `__start` symbol, emits a bounded polled UART
checkpoint, and parks **before `start_first_cpu`, VM initialization, IOKit or
userspace**. It is an entry/console experiment, not a kernel/init boot. It does
not fix the Apple platform expert's GIC dependency. There is no hardware or
guest runtime evidence for this candidate. An embedded marker string is not
execution evidence.

## Source and build selection

- PureDarwin pin: `e7aa00a60f0b56ef35aa24252e81467787bf71ea`.
- Separate checkout: `sources/puredarwin-m1-firstboot`.
- Nix package: `kernel-arm64-t8103-firstboot`, defined by
  `nix/m1-firstboot.nix` and exposed by the patched `flake.nix`.
- Configuration: ARM64, MacOSX, DEBUG, `MACHINE_CONFIGS=T8103`,
  `ARM64_BOARD_CONFIG_T8103`, `-mcpu=apple-m1`, `__ARM_ARCH_8_2__=1`,
  `DARWIN_LAB_M1_FIRSTBOOT=1`.
- Native H13 hardware definitions remain intact, including the `HAS_CTRR`
  hardware fact and 16 KiB pages. Only this experiment omits `PMAP_CS`,
  `PMAP_CS_ENABLE`, `XNU_MONITOR` and `KERNEL_INTEGRITY_CTRR`: the required
  private CoreEntitlements V2 ABI and CTRR register definitions are absent.
  This does not initialize or prove any hardware memory-protection mechanism.
  Normal T8103 preprocessing without `DARWIN_LAB_M1_FIRSTBOOT` is unchanged.
- `doconf` selects `config/MASTER.arm64.t8103`, derived from the pinned MacOSX
  file with `nos_arm_asm nos_arm_pmap` added to its non-SPTM ARM extras. Those
  options select the public assembly/pmap sources, including our actual entry.
  `config_pmap_ppl` remains: it selects the classic pmap/arm_init backend and
  does not itself enable `XNU_MONITOR`. No QEMUVIRT, VMAPPLE, GIC or PSCI platform
  is selected. This is explicitly not a supported normal-boot configuration.
- Native 16 KiB page configuration and `ARM_LARGE_MEMORY=1`;
  kernel link address `0xfffffe0007004000`, PLK segment `0xfffffe0004004000`,
  `VM_KERNEL_LINK_ADDRESS`/future KC base `0xfffffe0006000000`.
- Artifact for this experiment: the installed kernel Mach-O under
  `System/Library/Kernels/`. No KC, kexts, ramdisk or userspace is required:
  the entry never reads a Mach-O header, follows fixups or initializes XNU.
  These addresses do not make a raw kernel suitable for later normal VM boot.
- CMake refuses non-DEBUG T8103; the entry include independently refuses a
  non-T8103/non-DEBUG compilation. Both `__start` and `start_first_cpu` branch
  to the diagnostic entry; the experiment has no continue boot switch.

Root owns Nix orchestration. The build selection to review is
`path:/home/chrisk/src/darwin-lab/sources/puredarwin-m1-firstboot#kernel-arm64-t8103-firstboot`.
Using the `path:` source form includes the deliberately untracked additions;
a Git flake snapshot would need its own explicit inclusion step. No build was
started by the stager or the source reviewer.

## Entry and console contract

1. Select a T8103 M1 target. Root reports an authorized read-only inventory:
   Linux DT model reported as Apple MacBook Pro 13-inch M1, with compatibles
   `apple,j293` and `apple,t8103`; this now supports J293/T8103 identity. The
   MacBookPro17,1 marketing/firmware mapping is not a direct readout here.
2. Load the kernel's Mach-O segments into writable guest RAM with their relative
   VM layout preserved. Enter the physical address corresponding to its
   `LC_UNIXTHREAD` `__start` entry. Do not branch to `start_first_cpu` and do not
   apply Apple's normal boot patchfinder transformations to this entry.
   Load displacement must preserve at least 16 KiB alignment. The local ADRP
   vector reference assumes that the physical/linked displacement preserves
   page offsets. Make loaded instruction bytes visible before execution.
3. Enter AArch64 EL1 with stage-1 MMU disabled (`SCTLR_EL1.M=0`). The loader owns
   cache coherency and any prior MMU teardown; this code does not change those
   states. No stack is used. `x0` is the normal XNU `boot_args` pointer and is
   preserved but **not dereferenced in revision 3**. x1–x5, x9–x10 and x19 are
   clobbered. No extra register arguments are introduced.
4. Launch **one CPU only**. Do not start secondary CPUs. `cpus=1` below is
   documentation for a future normal path, not enforcement in this assembly.
5. Verify target ADT `/arm-io/uart0` translated `reg[0]` equals `0x235200000`.
   That fixed address is a pinned source fact for T8103, not a machine readout.
   Under m1n1, expose the Samsung VUART there and capture USB VUART or the
   `HVLOG:` pass-through. Under bare metal, that UART must already be clocked,
   configured and externally captured. Bare-metal operation is a separate,
   unqualified route; a hypervisor result would not qualify it.
6. Guest EL1 must be allowed to read CurrentEL, SCTLR_EL1 and MIDR_EL1 and write
   DAIF and VBAR_EL1. MIDR must identify Apple (implementer 0x61), Tonga Icestorm
   (part 0x022) or Firestorm (0x023). This check is not a model/SoC attestation.
7. The only peripheral accesses are 32-bit reads of UART `UTRSTAT` (+0x10),
   polling TXBE bit 1 and TXE bit 2, and 32-bit writes of bytes to `UTXH` (+0x20).
   The experiment does not reset the UART, alter its clock/baud, enable UART
   interrupts, or touch AIC/GIC. Each polling interval has a 1,048,576-iteration
   limit, which is an instruction budget, not a calibrated timeout.

Exact suggested boot argument string:

```text
-v debug=0x14e keepsyms=1 serial=3 serial-device-name=uart0 disable-uart-irq=1 cpus=1
```

**Revision 3 does not parse these arguments.** The dedicated compile selection
unconditionally enters and parks; arguments cannot enable/disable it or make it
continue. Boot-argument and ADT correctness for the later C initialization path
remain untested.

On the reached checkpoint, the candidate transmits exactly:

```text
HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm next=park kernel_init=not_started
```

The first diagnostic instruction masks DAIF before checking EL, so a mistaken
privileged-EL handoff also parks masked. EL0 is not a supported entry context.
The line is CRLF-terminated. It is emitted after EL/MMU/MIDR checks, through
polled UART stores from the linked XNU entry. After transmission it polls TXE
and parks in an unconditional branch loop with interrupts masked. No WFI/WFE,
function return or normal-boot fallthrough exists. A UART fault goes to the
existing early XNU spin vectors, preserving exception registers for inspection.

Optional register inspection: x19=0 at entry, 1 after CPU validation, 2 after
at least one UART store, 3 after the complete line and TXE; 0xe1 means wrong EL,
0xe2 MMU enabled, 0xe3 wrong CPU, 0xe4 polling exhausted. A fault can leave an
earlier value. A full captured line plus the expected parked PC/state supports
only this checkpoint; the string in a file, a loader log or x19 alone does not.

## Why this bounded path

The pinned source has a native T8103 board block but no supported machine/build
selection. Attempt 001 supplied that selection and then failed SDK compilation:
15 compiler errors came from three by-value `CEContext_t` fields in `pmap_cs.h`.
The V2 header in this distribution is an explicit opaque-type shim, not the
private API definition. No type layout or successful security operation is
invented here. Revision 2 excludes the unavailable monitor only in this parked
build, and selects the public backend sources omitted by generic MacOSX config.
Attempt 002 then terminated on `amcc_rorgn.c`'s missing `memmap_types.h`.
Removing only that include in a scratch SDK syntax check exposed 13 further
errors for missing CTRR register identifiers and masks. Revision 3 omits the
CTRR implementation selector only for the parked experiment. `HAS_CTRR` still
describes the hardware; there are no guessed register definitions or successful
lockdown stubs. Native T8103 preprocessing without the experiment flag remains
unchanged. See `BUILD_DIAGNOSIS.md` and `attempt-002-diagnosis.json`.
The Apple IOKit platform expert skips GIC initialization, but constructs
`PDArmCPU` objects whose `startForCPU()` requires `PDArmGIC_map_cpu()`. It cannot
honestly reach normal Apple CPU/interrupt service that way. Native CPU register,
PPL/CTRR, boot memory/ADT and clock setup also precede the normal C console.
Parking at both cold entry symbols makes none of those mechanisms into success
stubs. Restoring a supported monitor/API implementation and reviewing the full
boot contract are prerequisites to any subsequent candidate that can continue.

Source evidence in the pinned trees:

- XNU `osfmk/arm64/start.s`: `__start` and `start_first_cpu` entry contract;
  `osfmk/arm/arm_init.c`: `arm_vm_init()` precedes `serial_init()`.
- XNU `pexpert/pexpert/arm64/board_config.h`: native T8103 block;
  `H13.h`: 16 KiB and Apple core features;
  `osfmk/arm64/proc_reg.h`: `MIDR_TONGA_ICESTORM/FIRESTORM`.
- `PDArmPlatformExpert.cpp`: Apple early/late interrupt hooks;
  `PDArmCPU.cpp`: `startForCPU()`, `initCPU()`, `signalCPU()`;
  `PDArmGIC.cpp`: redistributor mapping and GIC IPI writes.
- m1n1 `121fcbf35d4501814b58ca5347926a4173b6a512`, `src/soc.h`: T8103 UART;
  `src/uart_regs.h`, `src/uart.c`: registers and poll/write sequence;
  `src/hv/hv_vuart.c`: Samsung emulation and `HVLOG:` pass-through;
  `proxyclient/m1n1/hv/__init__.py`: VUART mapping from ADT uart0.

## Staging and remaining validation

`stage.py --checkout <clean-isolated-checkout>` checks the exact pin, original
file bytes, patch hash, destination paths and whole patch applicability.
`--execute` applies it and verifies resulting hashes. It refuses the baseline
and RAM-probe checkouts, dirty trees, wrong pins, links and preexisting additions.
It does not fetch, build, boot or modify capabilities. `candidate.patch` and
`manifest.json` are the authoritative staged bytes; the two standalone source
files provide convenient review copies and are tested against those bytes.

Before any expensive build, review this scope and source/build selection. Then
root can build the selected package, record toolchain/source/artifact hashes,
and inspect the actual Mach-O entry branch and native build flags. The revised
diagnostic configuration's compilation/linking remains an SDK/build unknown
until that build succeeds;
do not silently substitute a VIRT kernel if it fails. The local assembly smoke
check is not an XNU SDK build.

Before an eventual authorized hardware experiment, the separate handoff owner
must resolve pinned m1n1 loading/patchfinder compatibility, guest EL1 state,
actual target ADT and console capture. Recovery and authorization remain root's
separate gate. No such operation is part of this source patch.

Root's reported installed chain is stage1 `v1.6.1-dirty`, stage2
`f5a82fdb-dirty`, OS firmware 13.5, system firmware unknown. Those versions do
not establish a match with the pinned host/target proxy ABI. The read-only
inventory is not permission or evidence for a boot. Packet review still gates
any hardware experiment.

Memory-probe code, mapped quarantine, userspace, existing baseline/ram-probe
checkouts and target capability profiles are unchanged by this integration.
