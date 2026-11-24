# SPDX-License-Identifier: MIT
# The lab's T8103 bring-up kernel with ./kernel-patches applied.
#
# Every collection in this lab links one prebuilt kernel,
#   /nix/store/1ys1x1hzkq3v0l8bc14v2bq1jm5i46zn-puredarwin-kernel-arm64-t8103-bringup-0.1
# (m1-display/console-return/build-kc.nix), whose recipe is m1-bringup/kernel-bringup.nix:
# mkSiliconForestBuild, target xnu, DEBUG, SILICONFOREST_ARM64_MACHINE_CONFIG=T8103. The flake does
# not export mkSiliconForestBuild, so this takes the flake's kernel-arm64-t8010-debug (the same
# call but for the machine config) and changes the machine config, the name and the source.
#
# The source and the three toolchain paths are the ones recorded in that kernel's derivation
# (nix derivation show /nix/store/dbhji9n78qw3kk70f8wxbi5i06jsz4b3-...-bringup-0.1.drv). With
# `--arg patched false` this evaluates to a derivation whose environment equals the pinned
# one in every variable but `out` (compared attribute by attribute; the output path differs
# because the inputs are named as store paths here and as derivations there).
#
#   nix-build kernel.nix --no-out-link                       # the patched kernel
#   nix-build kernel.nix --arg patched false --no-out-link   # control: the pinned recipe
#
# kernel-swap.nix puts the result into a collection recipe.
#
# `--argstr kernelConfig development`: the same source, patches and toolchain built as XNU's
# DEVELOPMENT configuration instead of DEBUG (plus kernel-patches/0004, which lets the tree's
# build accept that for T8103). The only difference
# in the recipe is -DSILICONFOREST_XNU_KERNEL_CONFIG, which is also the only difference between
# the flake's own release and debug kernels. DEBUG stays the default.
{ patched ? true, kernelConfig ? "debug" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/kernel.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/kernel.nix { inherit kernelConfig; } else
let
  # the tree refuses RELEASE for this machine (no serial console, no -unsafe_kernel_text)
  configs = { debug = "DEBUG"; development = "DEVELOPMENT"; };
  config = configs.${kernelConfig};
  configNeedle = "-DSILICONFOREST_XNU_KERNEL_CONFIG=DEBUG";
  pins = import ../m1-rust-kext/pins.nix;
  sp = builtins.storePath;
  toolchain = sp "/nix/store/c17dkymcla7drga7i6l4qpppfffwz54g-darwin-cross-toolchain-nix";
  base = pins.pd.packages.x86_64-linux.kernel-arm64-t8010-debug.override {
    arm64CrossToolchain = toolchain;
    nativeLd = sp "/nix/store/wq3qfxl2hqyp3afaw5lldazn6whsx57q-puredarwin-native-ld-0.1";
    compilerRtArm64 = sp "/nix/store/qk6gj9gmf1vlj523ps88v9qxjaglvi1w-puredarwin-compiler-rt-21.1.8";
  };
  needle = "MACHINE_CONFIG=T8010";
in base.overrideAttrs (old:
  assert builtins.stringLength old.configurePhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.configurePhase);
  {
    pname = "siliconforest-kernel-arm64-t8103-bringup";
    name = "siliconforest-kernel-arm64-t8103-bringup-0.1";
    src = sp "/nix/store/fzwhq5qb2cn381dd6hza7ffisclc1vxk-source";
    configurePhase =
      assert builtins.stringLength old.configurePhase
        != builtins.stringLength (builtins.replaceStrings [ configNeedle ] [ "" ] old.configurePhase);
      builtins.replaceStrings [ needle configNeedle ]
        [ "MACHINE_CONFIG=T8103" "-DSILICONFOREST_XNU_KERNEL_CONFIG=${config}" ] old.configurePhase;
  } // (if patched then {
    patches = [
      ./kernel-patches/0001-xnu-sflfile-descriptors-are-sendable.patch
      ./kernel-patches/0002-xnu-sflfile-select-poll-kevent-waiters.patch
      ./kernel-patches/0003-xnu-sflfile-mmap.patch
    ] ++ (if kernelConfig == "debug" then [ ] else [
      ./kernel-patches/0004-xnu-t8103-may-be-built-as-development.patch
    ]);
  } else { }))
