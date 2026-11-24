# Build failures and bounded candidate revisions

Attempt 001 terminated with compiler failures, before kernel linking. Its patch
SHA-256 is `c59f31d78be769059f915a003daf14b9f82c94ea34308affd98714885766f2a1`.
The exact patch, manifest, source-validation record, review sources and per-file
index hashes are retained in `attempts/001/`. The failed Nix source/build tree
and root's logs were read only; the checkout index was not changed by this fix.

The complete `runs/m1-bringup-007/kernel-build-001.log` contains **15 actual
compiler error diagnostics, representing three unique field errors**:

- `osfmk/vm/pmap_cs.h:226`: `CEContext_t profile_ctx_storage`.
- `osfmk/vm/pmap_cs.h:248`: `CEContext_t entitlements_ctx_storage`.
- `osfmk/vm/pmap_cs.h:412`: `CEContext_t core_entitlements_ctx`.

Each occurs in five compilations: `IOKitBSDInit.cpo`, `vfs_bio.o`,
`vfs_cluster.o`, `sleh.o`, and `commpage.o`. First diagnostics are log lines
80350, 80359 and 80371. Subsequent make/ninja/Nix errors propagate those
failures. Warning source lines containing the word `error:` are not additional
compiler failures. See `attempt-001-diagnosis.json` for the complete diagnostic
line list and log hash.

The cause is concrete: `EXTERNAL_HEADERS/CoreEntitlements/V2/API.h:2` identifies
itself as a PureDarwin shim, and line 14 only forward-declares `_CEContext`.
The V2 Kernel header likewise contains no implementation. The older
`EntitlementsPriv.h:23` defines `CEQueryContext`, a different API; it does not
establish V2 layout compatibility. `osfmk/arm/pmap/pmap.c:13655` onward also
needs V2 `CEElement_t`, `CEReturn_t` and `libCoreEntitlements` operations. An
invented context layout would only conceal the first missing interface.

There is a second source-selection issue even after those errors: generic
`config/MASTER.arm64.MacOSX:22` selects `config_pmap_ppl` without
`nos_arm_asm`/`nos_arm_pmap`. `osfmk/conf/files.arm64:10` requires
`nos_arm_pmap` for the public pmap implementation; line 62 requires the
assembly option for `start.s`. The generated attempt-001 osfmk Makefile lacks
both `pmap.o` and `start.o` in its object list. That build could not substantiate
our entry instrumentation. No claim is made that these are the only remaining
native build/link issues: compilation stopped before the full link.

Revision 2 makes three narrowly scoped changes beyond attempt 001:

1. `m1_first_entry.inc` masks DAIF as its first instruction, including wrong-EL
   refusal. The assembled-code tests check UART construction, MMIO widths and
   offsets, status bits, EL/MMU/MIDR checks, preserved x0, allowed instructions,
   branch destinations and a final self-loop. Ten assembled mutations are
   rejected, including calls, returns and fallthrough into marker data.
2. New `config/MASTER.arm64.t8103` copies the pinned MacOSX configuration and
   adds only `nos_arm_pmap nos_arm_asm` to its non-SPTM ARM extras. This selects
   actual public source implementations, including the patched XNU entry.
3. Only when `DARWIN_LAB_M1_FIRSTBOOT=1`, the T8103 board block omits the
   unavailable `PMAP_CS`, `PMAP_CS_ENABLE` and `XNU_MONITOR` subsystem. Native
   H13 features, CTRR and page geometry remain. A compiler preprocessing test
   verifies exactly those three omissions and verifies that normal T8103
   macros are otherwise byte-for-byte equivalent as definitions. Both cold
   entry symbols redirect to the console-and-park routine, so this restricted
   configuration cannot continue into normal VM initialization via either.

This is an honest build configuration for a **pre-VM entry experiment**, not a
code-signing or monitor implementation. No placeholder types, successful
validation stubs, GIC substitution or quarantine changes are added. A future
full-boot candidate must restore a supported security/backend contract before
removing the entry park. Root owns attempt 002 and any newly exposed errors.

## Attempt 002 and revision 3

Root confirmed attempt 002 terminal through its actual unified session 45606:
the command exited 1 and Nix reports builder exit 2. Its patch SHA-256 is
`cdc85391ef8c73ab06100a65c2fda6aed59091fa7d0db17c257fb69598f7fb44`.
`attempts/002/` preserves that exact patch, manifest, review sources, validation
record and all eight index file hashes. Its retained Nix build tree is
`/nix/var/nix/builds/nix-1033418-1122696851/build`. Neither that tree nor root's
jobs were changed by this diagnosis. PID absence in a separate process
namespace was not used to establish terminal status.

The new fatal diagnostic at log line 71707 is
`osfmk/arm64/amcc_rorgn.c:45:10: 'memmap_types.h' file not found`.
The selector chain is explicit:

1. The T8103 master selects `nos_arm_asm`.
2. `osfmk/conf/files.arm64:38` therefore selects `amcc_rorgn.c`.
3. H13's `HAS_CTRR` (`H13.h:35`) causes
   `apple_arm64_common.h:65-67` to define `KERNEL_INTEGRITY_CTRR`.
4. That selector enables the AMCC/CTRR implementation at `amcc_rorgn.c:31`,
   including the unavailable header at line 45.

The include itself has no obvious type use in this file, but removing it is
insufficient. A scratch-only SDK syntax check replayed attempt 002's actual
compiler arguments with only that include removed. It produced 13 errors for
missing `ARM64_REG_CTRR_*`, `ACC_CTRR_*`, and `CTRR_CTL_EL1_A_*` definitions.
These definitions are not present elsewhere in the pinned XNU sources. The
probe used a temporary source file and `-fsyntax-only`; no running source
snapshot or output object was modified. Exact command/diagnostics and scope
are referenced by `amcc-selector-analysis.json`.

Revision 3 adds one experiment-only omission: `#undef KERNEL_INTEGRITY_CTRR`
inside the T8103 board block under `DARWIN_LAB_M1_FIRSTBOOT`. It does not undefine
the hardware fact `HAS_CTRR`, alter any normal T8103 macros, or invent register
layouts. This leaves the AMCC/CTRR implementation unselected through its own
existing guards. A second scratch SDK syntax check with the candidate board
header confirms that exclusion; it is not an implementation or runtime test.

The compiled macro comparison now requires exactly four omitted backend
selectors (the three revision-2 PPL selectors and this CTRR selector), preserving
all remaining native definitions. Both cold entries still unconditionally
enter the same pre-VM console-and-park routine. No hardware lockdown or normal
kernel initialization is claimed. A future continuing kernel must restore and
validate the missing backend contracts before removing those entry branches.

## Attempt 003: an internal assembler register spelling

Attempt 003 was a terminal compiler/assembler failure. Its actual error sites
are `start.s:818` and `start.s:820`: open-source Clang does not recognize the
`CPU_OVRD` spelling. Source and evidence are preserved in `attempts/003/`.

Revision 4 adds an assembly-only alias in the experiment's T8103 board block.
Pinned m1n1 `src/cpu_regs.h` defines CYC_OVRD as `sys_reg(3,5,15,5,0)`, and its
IRQ/FIQ, WFI and snoop field definitions match XNU's `ARM64_REG_CYC_OVRD_*`
definitions. Claude independently checked those source relationships and
assembled the read/write forms. The XNU-name equivalence remains an inference;
no claim is made of an Apple-provided alias definition or hardware validation.

The shared board-header alias covers both `start.s` and idle/sleep uses in
`machine_routines_asm.s`. It does not set `NO_CPU_OVRD` or remove instructions.
An added focused test independently encodes the tuple and verifies read/write
opcodes, and confirms that the alias is absent from a normal T8103 selection.
All ten focused checks pass, including ten rejected entry-code mutations.
These register accesses remain unreachable behind the mandatory entry park.
Root owns the full build.

## Attempt 004 and revision 5

Attempt 004 failed assembly at machine_routines_asm.s:155/159 (CPU_CFG) and
:1603/1616 (ACC_OVRD). Revision 5 added experiment-only spellings using pinned
m1n1 ACC_CFG sys_reg(3,5,15,4,0) and ACC_OVRD sys_reg(3,5,15,6,0).
The branch predictor sleep and deep-sleep fields match the XNU uses. Claude
independently reviewed these inferred aliases; the focused test independently
encodes and compares all six read/write opcodes. This is source-based inference,
not an Apple internal header or hardware result. These accesses are unreachable
in the parked path. Attempt 004 is preserved in attempts/004/.

## Attempt 005 and revision 6

Attempt 005 compiled the selected native sources and failed final linking on
five symbols: CleanPoC_DcacheRegion_Force_nopreempt,
CleanPoC_DcacheRegion_Force_nopreempt_nohid, enable_dc_mva_ops,
disable_dc_mva_ops, and pmap_pending_preemption. The first four public Apple
implementations are absent. The last is a debug wrapper selected by
SCHED_HYGIENE_DEBUG; the real inline AST_URGENT check exists in pmap.h.
See reports/sprint-007/kernel-build-005.json and attempts/005/.

Revision 6 removes optional sched_hygiene_debug from this isolated master.
It supplies the four unavailable cache symbols as a clearly labeled,
nonreturning unsupported-operation failure path in the entry include. Its
actual assembled bytes mask DAIF, set x19=0xef and self-loop. It never returns,
performs cache maintenance, touches HID registers or reports success. This is
an explicit diagnostic linkage boundary, not a cache implementation. A new
assembled-symbol test proves all four names address those exact three
instructions. The existing entry control-flow tests still prohibit any escape
or normal boot. Eleven focused tests pass. The full build remains separate.
