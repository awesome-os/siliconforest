# Attempt 001 failure and revision 2 scope

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
