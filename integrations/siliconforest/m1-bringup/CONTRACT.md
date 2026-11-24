# Increment009: continuing native T8103 kernel candidate

The objective is a verified command line on the real M1 under m1n1 HV. This
candidate restores the normal kernel initialization path. Entry output, a VM
checkpoint, a successful link, or a parked exception is not completion of that
objective. Root's actual Nix attempt001 built the kernel successfully; its
receipt is `reports/sprint-009/kernel-build-001.json`. Kernel SHA-256 is
`ce5c7ee376174ba2424d9d044ddcbebf66fefbb40a15796d595a907d7d4e4537`.
That revision has now run on the real M1 under m1n1 HV: attempt004 reached
progress 6, printed the VM-return checkpoint and Darwin banner, and stopped
on a recount allocator-corruption panic during IOKit thread creation. This is
VM/startup evidence, not a command line. The final private capture is under
`runs/m1-bringup-009/attempt-004/guest`; the source diagnosis is recorded in
`reports/sprint-009/recount-panic-analysis.json`. Revision002 below is built and
has executed in attempt006. It passed the earlier recount failure and reached
BSD/DTrace initialization, where a missing guest boot-policy property caused
a separate assertion. A guest-only metadata fix then enabled attempt008 to mount
root and run the single-user shell. Its fresh Darwin/arm64/25.5.0 response and
Linux recovery were independently verified; the original strict false-negative
receipt caused by interleaved stderr is preserved in the private capture.

The source pin is PureDarwin `e7aa00a60f0b56ef35aa24252e81467787bf71ea`.
The host/target proxy pin remains m1n1
`121fcbf35d4501814b58ca5347926a4173b6a512`, with the observed 20-byte
CPUFeatures ABI. The isolated checkout is `sources/puredarwin-m1-bringup`.
Frozen increment007/008 loaders, kernels, recovery controllers and checkouts
are not modified. This is an HV guest development configuration; bare-metal
boot, production protection and secondary CPU bring-up are outside its claim.

## Source and build selection

`manifest.json` binds every before/after file and `candidate.patch`. Revision002
contains 14 files, patch SHA-256
`e6d7771b818b70a2459db9143ae27979f05d28b84f5fd4027a7941e8fe9e3fcf`.
It adds only the recount allocation/API corrections described below to the
12-file revision001 kernel candidate. Revision001's patch, manifest, contract,
tests and validation are preserved byte-for-byte under `revisions/001/`;
its patch SHA-256 remains
`b20e32b6798585b4a33d347ad222ce11912354d393358ebbb3a965493d3d0529`.
Root captured revision001 for Nix attempt001 at
`/nix/store/a1lrkmvpxciil4fr087wj8zg33vqdma8-source`.

The flake attribute is `kernel-arm64-t8103-bringup`; its recipe is
`nix/m1-bringup.nix`. It selects `DEBUG_ARM64_T8103`, arm64 (not arm64e),
`-mcpu=apple-m1`, `ARM64_BOARD_CONFIG_T8103`, `DARWIN_LAB_M1_BRINGUP=1`,
`__ARM_ARCH_8_2__=1`, and `ARM_LARGE_MEMORY=1`. The page size is 16 KiB.
Kernel link address is `0xfffffe0007004000`, prelink address
`0xfffffe0004004000`, and KC base/`VM_KERNEL_LINK_ADDRESS`
`0xfffffe0006000000`.

The DEBUG master selects actual public `nos_arm_pmap` and `nos_arm_asm`
sources. `config_pmap_ppl` is also needed to select the normal `start.s`;
it does not itself assert `XNU_MONITOR`. Optional `sched_hygiene_debug` is
omitted, selecting the existing AST_URGENT inline preemption implementation
instead of an unavailable debug wrapper. Assertions and normal failures remain.

The board preserves native H13 definitions, `HAS_CTRR`, `HAS_CLUSTER`,
`HAS_IPI`, `USE_APPLEARMSMP` and the actual M1 CPU family. It is not an Apple
virtual platform or a GIC platform. Only this explicit development selection
omits `KERNEL_INTEGRITY_CTRR`, `XNU_MONITOR`, `PMAP_CS`, and `PMAP_CS_ENABLE`.
The private CTRR backend requires absent `memmap_types.h`; the private PPL
code-signing interface requires an unavailable by-value CoreEntitlements V2
type. No stand-in types, success functions or fake lockdown are supplied.
Public non-PPL paths perform normal VM work; they do not provide PPL/CTRR
security. `arm_vm_init.c` expressly handles large-memory non-PPL configurations.
The explicit build-bound protection decision is also recorded in
`reports/sprint-009/kernel-protection-decision.json`: this candidate does not
claim hardware-locked kernel text or PPL protection.
`ml_lockdown_init()` calls `rorgn_stash_range()` only under the omitted
implementation selectors, not merely `HAS_CTRR`. Root's existing real M1 ADT
capture has no `/chosen/lock-regs` property, consistent with refusing to claim
that backend is available.

## Entry and handoff

Both cold entries retain the upstream path: `__start` branches to
`start_first_cpu`, which sets up page tables, enables the stage-1 MMU and
enters `arm_init`. No entry redirect or mandatory park remains. The host must
validate live J293/T8103, one running Icestorm or Firestorm CPU, EL1,
SCTLR_EL12.M=0, boot-argument revision/ranges, DRAM ownership and UART0
`0x235200000` before entering. Only one guest CPU may be launched; `cpus=1`
must also constrain XNU topology.

**Execution requires a native MH_FILESET KC**, accepted by root's independent
normal-image inspector. A generic VIRT kernel or the old parked image is not
a substitute. `arm_slide_rebase_and_sign_image()` at
`osfmk/arm/arm_init.c:233` performs fileset chained fixups and adjusts headers.
Its static-kernel alternative assumes iBoot already slid load-command VM
addresses. Pinned `HV.load_macho()` does not do this for an ordinary M1 image.
Loading a bare MH_EXECUTE unchanged is therefore not an accepted handoff.
Root owns KC validation, image placement, original metadata retention and
BootArgs/ADT construction. The loader must preserve 16-KiB alignment, coherent
VA/PA displacement, valid KC segment layout, TrustCache, firmware ranges and
the `rd=md0` memory-disk descriptor. No physical firmware bytes belong here.

**The guest ADT must be relocated after the KC for this non-PPL build.** The
stock HV loader places it before TrustCache/KC. With the CTRR implementation
omitted, `SecureDTIsLockedDown()` returns false and that ADT is not part of
EXTRADATA. `arm_vm_prot_finalize()` frees memory below `segLOWEST`, including
the stock ADT location. Place the live guest ADT in the post-kernel reservation
covered by `topOfKernelData`, updating `hv.adt_base`, the physical `DeviceTree`
range and BootArgs' virtual `devtree`, size and reservation end coherently.
Root's current loader also copies TrustCache into that reservation, which this
DEBUG kernel accepts (`arm_vm_init.c:1349–1353` rejects it only outside
DEBUG/DEVELOPMENT). Validate the complete reservation before writes.

The normal-console boot argument base is:

```
-v debug=0x14e keepsyms=1 serial=3 serial-device-name=uart0 disable-uart-irq=1 cpus=1 rd=md0
```

Root/Grok bind the actual root image and init/shell contract. Console transport
is Samsung UART0 through m1n1 USB VUART (secondary CDC control/data interfaces
2/3), with real duplex input. The early 007 HVLOG capture and park classifier
are not the normal-console acceptance path.

Observed existing ADT facts supplied by root: cpu0 is Icestorm, reg0, running,
one interrupt `[70]`, parent AIC phandle112, 24 MHz timebase/clock/bus. Seven
other CPUs are waiting and are removed by the one-CPU HV setup. AIC is `aic,1`,
PMGR phandle135 is `pmgr1,t8103`, and uart0 phandle165 has IRQ605 under AIC112.
Private provenance remains in `runs/m1-bringup-009/adt-platform-selection.json`.

## Genuine cache operations

`osfmk/arm64/m1_cache_helpers.inc`, included by `caches_asm.s`, implements:

| Function | Operation |
| --- | --- |
| `enable_dc_mva_ops` | Clear selected core HID4/EHID4 bit11, preserving unrelated bits and incoming DAIF. |
| `disable_dc_mva_ops` | Complete prior maintenance, set selected core bit11, preserve unrelated bits and DAIF. |
| `CleanPoC_DcacheRegion_Force_nopreempt` | Save exact HID/DAIF, mask asynchronous exceptions, temporarily clear bit11, execute the real upstream clean/invalidate range macro, restore exact HID/DAIF. |
| `CleanPoC_DcacheRegion_Force_nopreempt_nohid` | Execute the upstream range implementation after the caller enabled maintenance; no HID/DAIF writes. |

Callers own preemption: it must be disabled and remain disabled across a batch.
The single-range wrapper restores an already-enabled HID state, so it does not
break a surrounding batch. All wrappers preserve x18–x30 and SP, use no stack
or calls, and return normally after real work. The range macro clobbers
x0/x1/x2/x9 and condition flags; wrapper temporaries are x10–x13. Empty ranges
perform a completion barrier and return without underflowing the length-1
loop. Nonempty ranges must be valid non-wrapping kernel VA ranges. Existing
XNU M1 `MMU_CLINE=6` selects 64-byte maintenance strides.

Apple's existing macro uses **DC CIVAC**, with DSB SY before/after the range;
DC CVAC is documented as a NOP on this hardware. HID changes use ISB. Encoding
sources in pinned m1n1 `src/cpu_regs.h`: HID4 `(3,0,15,4,0)` at161,
EHID4 `(3,0,15,4,1)` at217, bit11 at173/231. Pinned `hv_exc.c:235–236`
passes those accesses through; m1n1 `chickens.c:221–223` uses the same disable
bits. Source-supported CPU_OVRD/CPU_CFG/ACC_OVRD aliases are
`S3_5_C15_C5_0`, `S3_5_C15_C4_0`, `S3_5_C15_C6_0`; CPU_OVRD/CPU_CFG naming
is inferred from matching CYC_OVRD/ACC_CFG fields. Their normal/idle paths
have not been runtime-qualified by the earlier park experiment.

## Continuing progress and VM corrections

An inline, bounded UART loop runs after the original entry has saved BootArgs
in x20 and installed the low exception vector. It emits:

```
DARWIN_LAB_M1_BRINGUP checkpoint=start_first_cpu next=bootstrap_tables
```

Both completion and polling exhaustion continue to the original BootArgs
loads. No UART output is attempted at physical addresses after MMU enable.
The `darwin_lab_m1_progress` volatile global records reached C points:
1 = entered `arm_init`; 2 = image slide/fixup returned; 3 = initial PE setup
returned; 4 = about to call `arm_vm_init`; 5 = VM init returned; 6 = serial
and kprintf startup reached. These are diagnostic RAM values, not success
tokens. After actual serial initialization a second line reports
`checkpoint=arm_vm_init_returned next=kernel_init`; initialization continues.

Actual ADT AuxKC descriptors use complete `(-1,-1)` sentinels. Before
`phystokv`, 009 recognizes absence only when **both** AuxKC and its header
have all four fields equal to UINT64_MAX. Any mixed sentinel panics. Valid
physical mappings and their existing bounds checks are unchanged.

## Revision002: sparse recount CPU-kind indices

The one-CPU guest keeps the real Icestorm E-core identity. Recount maps it to
`RCT_CPU_EFFICIENCY == 1`, but the pinned startup code sized its CPU-kind
arrays with `popcount(1 << 1) == 1`. Thread accounting consequently wrote
`tracks[1]` beyond a one-track allocation. Attempt004 detected a nonzero free
slot in `recount_track_cpu_kind` while allocating another IOKit thread's
track. Its first word matches the sequence counter at offset 4, followed by
kernel time/instruction/cycle fields. The runtime fault and source defect are
established; the specific earlier writing thread was not traced.

`osfmk/kern/recount.c` now allocates through the highest represented CPU-kind
index. E-only and mixed P/E topologies both need two slots; P-only needs one.
The unused P slot remains zero. Raw enum values, CPU identification, accounting,
zone zero validation and all existing allocation/free paths are preserved.
Empty or out-of-range index spans still fail validation.

`bsd/kern/sys_recount.c` also bounds `proc_pidthreadcounts` output by the actual
performance-level count, matching `_selfcounts_perf_level`. Storage slots
include enum holes; exported levels remain dense. An E-only system therefore
exports one level whose counters come from slot 1, without reporting a fake P
level. These are general corrections in this isolated candidate, not an E-to-P
relabeling or a diagnostic-check bypass.

Focused host tests compile the actual startup/index/level-mapping C and track
layout with minimal host fixtures. P-only, E-only and mixed configurations
each run with one through four CPUs per represented kind. A neighboring free
slot sentinel must remain unchanged, holes must stay zero, and both export
counts must match represented levels. Empty and unknown topologies fail.
Restoring the original popcount allocation passes P-only/mixed tests but
corrupts the E-only sentinel, which the mutation test detects. This is host
model evidence; it does not claim the rebuilt kernel has run.

The loader must separately supply 256 fresh CSPRNG bytes in the guest-only
`/chosen/random-seed` property as raw bytes, with final ADT capacity checked.
Attempt003 captured the pinned kernel's real 256-versus-64-byte panic; the
guest-only loader correction allowed attempt004 to pass VM initialization.
The getter consumes and zeros the property, so it is not a validation probe.
No original firmware seed or new seed material belongs in public reports.

The DEBUG guest also requires `/chosen/kernel-ctrr-to-be-enabled` as an exact
four-byte little-endian integer. Attempt006 reached `sdt_init()` and asserted
in `ml_unsafe_kernel_text()`: its TUNABLES initializer leaves the policy
uninitialized if this property is missing or the wrong size. The saved original
ADT has no such property. The continuing loader supplies zero in the guest ADT
only, matching this build's explicitly omitted CTRR/PPL implementation. It
requires the hash-verified build receipt to declare that selection and both
protection claims false. The kernel assertion and ordinary VM permissions stay
intact. This metadata fix needs no new kernel build; its runtime result is
recorded separately in `reports/sprint-009/attempt006-policy-fix.json`.
The zero policy permits DTrace provider registration during startup. Later
root-enabled SDT/FBT probes may patch text, consistent with this unprotected
DEBUG build; the loader does not enable such probes.

## Remaining implementation and evidence gates

1. Root built revision002 and linked the new native fileset KC. Attempt006
   executed it beyond the recount failure; see `hardware-attempt-006.json`.
   `kc-bringup.nix` substitutes Claude's independently owned platform bundle
   into the generic kext set; its staged files are not part of this patch.
2. Root captures execution beyond the recount failure, retaining native
   AppleARMSMP registration, AIC phandle registration and the WFI-only PMGR.
   The platform's later startup and idle paths remain unqualified.
3. Root captures normal VUART input/output, root mount and userspace.
   A command with unpredictable input and its expected output is required for
   the command-line claim. Attempt008 supplies this evidence through independent
   raw-response review. Boot/reset/recovery evidence stays separate. Automatic
   strict verdict qualification uses a later command that redirects only uname
   stderr; it never filters diagnostic lines from a captured response.

Focused host validation: `python3 integrations/puredarwin/m1-bringup/test_bringup.py -v`.
It assembles actual source macros/wrappers and interprets their disassembly
for range, register and control-flow checks. This models instructions and
records maintenance/barriers; it does not execute privileged operations or
prove cache effectiveness. It also compiles the actual sentinel and recount
branches as host C, verifies native feature selection and exact-pin staging,
and assembles the continuing progress fragment. Root runs required repository
checks and Nix builds. Memory-mapping quarantine remains unchanged.
