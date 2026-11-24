# SPDX-License-Identifier: MIT
# The tree's arm64 kexts built from the corecrypto source with ./corecrypto-patches on top.
# Only corecrypto.kext is taken from the result (./build-kc-desktop.nix). The toolchain keeps
# the unpatched source, so nothing but the kext set is rebuilt.
let
  p = import ../m1-rust-kext/pins.nix;
in
p.pd.packages.x86_64-linux.kexts-arm64.override {
  corecryptoSrc = p.pkgs.applyPatches {
    name = "siliconforest-corecrypto-source-patched";
    src = p.pd.inputs.corecrypto-src;
    patches = [
      ./corecrypto-patches/0001-ccdrbg-nistctr-state-size-covers-the-whole-layout.patch
    ];
  };
}
