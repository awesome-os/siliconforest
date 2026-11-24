# The Rust half of the proof: ../m1-rust-kext/lkpi's bindgen step and module matrix, re-run
# on the patched DRM headers. Nothing of that recipe is copied; its derivations are
# re-instantiated with other inputs (`subst`, see default.nix).
{ pins, lkpi, lib, pkgs, drmkmod, subst, srcPairs }:
let
  # ../m1-rust-kext/lkpi/helpers replaces Linux's rust/helpers/drm.c with a copy that leaves
  # out the GEM shmem helpers, because drm-kmod had no drm_gem_shmem_helper.h. With the
  # helper ported, Linux's own file compiles, so the replacement is dropped here and
  # CONFIG_DRM_GEM_SHMEM_HELPER is added to the helper defines. (README.md, "For
  # m1-rust-kext", lists everything this file undoes or adds on top of that directory.)
  theirHelpers = ../m1-rust-kext/lkpi/helpers;
  helpersDir = pkgs.runCommand "sf-lkpi-rust-helpers-drm72" { } ''
    mkdir -p $out
    cp ${theirHelpers}/*.c $out/
    rm $out/drm.c
    # dma_fence_init64() written as dma_fence_init() for the 6.14 dma-fence ("goes away when
    # the DRM C core is brought to 7.2"): it is a real function now, and that stand-in would
    # lose the 64-bit sequence number flag
    rm -f $out/sf_dma_fence.c
    # atomic_sub and noop_llseek, see the file
    cp ${./rust-helpers/drm72.c} $out/sf_drm72.c
  '';

  bindingsGen = (subst (srcPairs ++ [ { from = theirHelpers; to = helpersDir; } ]) lkpi.bindingsGen).overrideAttrs (old:
    let buildPhase = builtins.replaceStrings [ "HD=(" ] [ "HD=(-DCONFIG_DRM_GEM_SHMEM_HELPER " ] old.buildPhase; in
    # the recipe must still build its helper defines the way this expects
    assert buildPhase != old.buildPhase;
    { name = "sf-lkpi-rust-bindings-drm72"; inherit buildPhase; });

  bgPair = [ { from = lkpi.bindingsGen; to = bindingsGen; } ];

  # ../m1-rust-kext/lkpi/patches edits the Rust tree where LinuxKPI differs from Linux. One of
  # those edits is transitional for exactly what this ticket fixes on the C side: it gives
  # kernel::dma_fence's ops table the three dma_fence_ops fields Linux 7.2 removed. Every
  # hunk on kernel/dma_fence.rs and anything under kernel/drm/ is dropped here, so the
  # wrappers measured are Linux's files, byte for byte (checked below).
  theirPatches = ../m1-rust-kext/lkpi/patches;
  patchesDir = pkgs.runCommand "sf-lkpi-rust-patches-drm72" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    mkdir -p $out
    python3 ${./tools/drop-wrapper-hunks.py} ${theirPatches} $out
  '';
  # errno gating reads the generated bindings, so the Rust tree is re-derived as well
  rustTree = (subst (bgPair ++ [ { from = theirPatches; to = patchesDir; } ]) lkpi.rustTree).overrideAttrs (old: {
    name = "linux-rust-for-pd-lkpi-drm72";
    buildCommand = old.buildCommand + ''
      # the wrappers this ticket is measured with are unmodified. (tools/gate.py adds
      # #[cfg(sf_mod_*)] lines to mod.rs files only; those are not wrapper code.)
      for f in dma_fence.rs drm/sched.rs drm/syncobj.rs drm/file.rs drm/device.rs drm/driver.rs \
               drm/mm.rs drm/ioctl.rs drm/gem/shmem.rs drm/gpuvm/sm_ops.rs drm/gpuvm/va.rs drm/gpuvm/vm_bo.rs; do
        cmp ${lkpi.linuxRust}/kernel/$f $out/kernel/$f
      done
      for f in drm/gem/mod.rs drm/gpuvm/mod.rs drm/mod.rs; do
        if diff ${lkpi.linuxRust}/kernel/$f $out/kernel/$f | grep '^[<>]' | grep -v '^> *#\[cfg(sf_mod_[a-z_0-9]*)\]$'; then
          echo "kernel/$f differs from Linux by more than module gates" >&2; exit 1
        fi
      done
    '';
  });
  bindings = subst bgPair lkpi.bindings;
  uapi = subst bgPair lkpi.uapi;

  wrappers = [
    "dma_fence" "drm::device" "drm::driver" "drm::file" "drm::ioctl" "drm::gem" "drm::mm"
    "drm::sched" "drm::syncobj" "drm::gpuvm" "drm::gem::shmem"
  ];
  matrix = (subst (bgPair ++ [
    { from = lkpi.bindings; to = bindings; }
    { from = lkpi.uapi; to = uapi; }
    { from = lkpi.rustTree; to = rustTree; }
  ]) (lkpi.matrixOnly wrappers)).overrideAttrs (old: {
    name = "sf-lkpi-kernel-crate-matrix-drm72";
    # the table this ticket reports: errors in each wrapper's own files vs. elsewhere
    buildPhase = old.buildPhase + ''
      python3 ${./tools/wrapper-status.py} $out | tee $out/wrappers.md
    '';
  });
in
{
  inherit bindingsGen helpersDir rustTree bindings uapi matrix wrappers;
}
