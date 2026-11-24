# SPDX-License-Identifier: MIT
# SFArmPlatformExpert.kext of the default desktop collection (../m1-smp/build-platform.nix)
# with ./platform-patches on top: the secondary cores of a native boot (sfcpuheld=1).
let
  pkgs = (import ../m1-rust-kext/pins.nix).pkgs;
in
(import ../m1-smp/build-platform.nix).overrideAttrs (old: {
  pname = "siliconforest-m1-native-boot-platform";
  src = pkgs.applyPatches {
    name = "siliconforest-m1-native-boot-platform-source";
    src = old.src;
    patches = [
      ./platform-patches/0001-SFAppleBootCPUPMGR-release-cores-held-by-the-boot-loader.patch
    ];
  };
})
