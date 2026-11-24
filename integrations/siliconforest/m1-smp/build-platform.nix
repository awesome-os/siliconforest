# SPDX-License-Identifier: MIT
# SFArmPlatformExpert.kext as ../m1-bringup-platform/build-platform.nix builds it (the
# baseline + its platform.patch), with ./platform-patches on top.
let
  pkgs = (import ../m1-rust-kext/pins.nix).pkgs;
in
(import ../m1-bringup-platform/build-platform.nix).overrideAttrs (old: {
  pname = "siliconforest-m1-smp-platform";
  src = pkgs.applyPatches {
    name = "siliconforest-m1-smp-platform-source";
    src = old.src;
    patches = [
      ./platform-patches/0001-SFAppleBootCPUPMGR-start-secondary-cores.patch
      ./platform-patches/0002-SFPerfControl-apple-start-after-all-cpus-initialized.patch
      ./platform-patches/0003-SFAppleBootCPUPMGR-accept-core-and-cluster-power-down.patch
      ./platform-patches/0004-SFAppleAIC-handler-barriers-for-disable-and-unregister.patch
    ];
  };
})
