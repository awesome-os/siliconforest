# SPDX-License-Identifier: MIT
# The current default collection (../m1-touchbar/build-kc.nix) plus SFAppleNVMe.kext
# (./build-kext.nix: READ-ONLY driver for the M1's internal NVMe storage), appended the same
# way and with its debug map stripped. Without sfnvme=<1..3> in the boot-args the driver
# does not start and the internal storage is not touched. No default collection is changed
# by this file.
#   nix-build ... build-kc.nix --argstr kernelConfig development --no-out-link
# Check the layout afterwards: ../m1-rust-kext/tools/kc_overlap.py <result>/kernel
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; more = [ { name = "SFAppleNVMe"; from = import ./build-kext.nix; } ]; } else
let
  pins = import ../m1-rust-kext/pins.nix;
  pkgs = pins.pkgs;
  base = import ../m1-touchbar/build-kc.nix { inherit kernelConfig; };
  strip = "${pins.crossToolchain}/bin/arm64-apple-darwin20.4-strip";
  full = import ./build-kext.nix;
  kext = pkgs.runCommand "${full.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${full}/System/Library/Extensions/SFAppleNVMe.kext $out/bundle.kext
    chmod -R u+w $out
    ${strip} -S $out/bundle.kext/Contents/MacOS/SFAppleNVMe
  '';
  needle = "-o kernel";
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''-kext "${kext}/bundle.kext" \
      -o kernel'' ] old.buildPhase;
  })
