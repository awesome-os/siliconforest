# SPDX-License-Identifier: MIT
# The native-boot collection (../build-kc.nix: the default desktop collection with the
# platform kext that can release held cores) with the SFCpuPower.kext of ./build-cpupower.nix
# in place of ../../m1-cpufreq's; everything else is the same build. With no further
# boot-args it logs the PMU's boot state once (sfpmu=1, nothing written) and makes
# reboot(2) restart and power off the machine through the SMC; sfpmu=2 also writes what
# Linux writes (SFAppleReboot.cpp); sfreboot=0 sfpmu=0 is the collection below.
#   nix-build build-kc.nix --no-out-link
# The kext is found among the collection's `-kext` paths by what the bundle holds and
# replaced by a copy of ours stripped the same way, as ../build-kc.nix does for the
# platform kext.
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../../rebase/pd.nix), where this is
# ../../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../../rebase/pd.nix).tip then import ../../rebase/build-kc.nix { inherit kernelConfig; platform = import ../build-platform.nix; cpupower = import ./build-cpupower.nix; } else
let
  pins = import ../../m1-rust-kext/pins.nix;
  pkgs = pins.pkgs;
  base = import ../build-kc.nix { inherit kernelConfig; };
  strip = "${pins.crossToolchain}/bin/arm64-apple-darwin20.4-strip";
  full = import ./build-cpupower.nix;
  ours = pkgs.runCommand "${full.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${full}/System/Library/Extensions/SFCpuPower.kext $out/bundle.kext
    chmod -R u+w $out
    ${strip} -S $out/bundle.kext/Contents/MacOS/SFCpuPower
  '';
  named = map builtins.unsafeDiscardStringContext (builtins.concatLists (builtins.filter builtins.isList
    (builtins.split ''-kext "(/nix/store/[^"]+[.]kext)"'' base.buildPhase)));
  theirs = builtins.filter (p: builtins.pathExists (p + "/Contents/MacOS/SFCpuPower")) named;
in assert builtins.length theirs == 1;
base.overrideAttrs (old:
  let phase = builtins.replaceStrings [ ''-kext "${builtins.head theirs}"'' ] [ ''-kext "${ours}/bundle.kext"'' ] old.buildPhase;
  in assert phase != old.buildPhase; { buildPhase = phase; })
