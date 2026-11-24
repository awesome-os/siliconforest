Claude bounded pre-load review (read-only; inspected the actual built bundle; no builds or writes).
VERDICT: no runtime blocker in core/mapping_ticket.*, DLMemoryProbe/* or the smoke recipe. One link-time item is worth fixing before the KC build.

ACTIONABLE (load/link)
1. DLMemoryProbe imports _kauth_cred_issuser (DLMemoryProbe.cpp:44; it shows in nm -u of the built bundle). In the pinned XNU config that symbol is exported ONLY by Unsupported.exports:99 and Private.exports:1225, not by BSDKernel. Info.plist declares bsd, iokit, libkern, mach and PDDRMDri, but not com.apple.kpi.unsupported. The baseline's hfs.kext, which also uses kauth_cred_issuser, declares <key>com.apple.kpi.unsupported</key><string>8.0.0</string>.
   If kc-builder enforces per-dependency exports, the KC link fails (fails closed). If it binds loosely, nothing breaks.
   Fix: add that dependency to Info.plist (same version as hfs). Otherwise, confirm the KC build log reports no unresolved or undeclared symbol for DLMemoryProbe.
   Every other import resolves through a declared KPI:
   - IOKit.exports: IOFree, IOLog, PE_parse_boot_argn, and IOMallocZero (as the alias _IOMallocZero:_IOMallocZero_external, which 10 baseline kexts also use)
   - IOKit.arm64.exports: inTaskWithOptions
   - Libkern(.arm64).exports: lck_*, bzero, memcpy, memset, PAGE_SHIFT_CONST
   - Mach.exports: kernel_task
   - BSDKernel.exports: kauth_cred_get
   - PDDRMDri: _pd_dri_register and _pd_dri_make_node are defined (T); its CFBundleVersion and OSBundleCompatibleVersion are both 1.0.0, which satisfies our 1.0.0 dependency.
   dyld_stub_binder is undefined in our bundle, but also in amfi, apfs, corecrypto and others, so it is a baseline norm. The bundle is MH_BUNDLE ARM64 with the same header flags as PDDRMDri, has _kmod_info, _dlmp_start and _dlmp_stop, and its plist is identical to the repo.

CHECKED, NO HAZARD
- mapping_ticket: _Atomic(bool) with __c11_atomic_* (Clang) or __atomic_* (GCC) keeps the old init, store-release and load-acquire semantics. A byte store-release (stlrb) and load-acquire (ldarb) need no LSE, so they are fine at the -mcpu=apple-a10 baseline and in any callback context (no locks).
  No C++ TU includes mapping_ticket.h, since _Atomic is C-only. The C++ side only uses dl_probe_size(), so a C/C++ layout mismatch is impossible.
- Adapter: the vm/memory_types.h include and the 88/48 static_assert match the protocol header and my ARM64 smoke binary (STATS 0xC0584472, COLLECT 0xC0304473). Start and ownership are unchanged: KERN_FAILURE only before registration, no unload, and service, tickets and quarantined backing are kept until reboot. Blocking calls (IOBMD allocation, complete and release inside COLLECT) run only under the sleeping lck_mtx. The pager callback is only an atomic store.
- Recipe: the standalone and staged builds use the same callPackage interface. Every failure path fails closed.

LIMITATIONS (unverified until the VM run)
- Whether pd_dri_register and pd_dri_make_node work from our kmod start before PDDRMDri's own service is up. Failure is logged ("registration rejected" / "publication N") and leaves no node, so the smoke reports FAILED at open. Capture the "RAM probe publication 0" IOLog line.
- The ioctl size and data pointer PDDRMDri passes to ioctl_probe. A mismatch makes every ioctl return EINVAL, and the smoke reports FAILED at identity.
- Kernel-side pager callback timing and teardown sufficiency (quarantine still holds), and whether kc-builder enforces KPI membership (item 1).
- Recipe staging: if the image is built through the git flake, the staged lab/ files, including userspace/memory_probe_result.h, must be git-tracked, or the recipe stops with "missing lab source". Record the sha256 of the image's /usr/bin/memory-probe-smoke. With byte-identical staged sources and the same toolchain it should match my scratch build, a62376d6afca3176151a7b93e91eda75df5a3eaf83b88e761cd78b606b6c42f8 (expected, not proven: verify before relying on it).
