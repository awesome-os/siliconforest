# SPDX-License-Identifier: MIT
# The default desktop collection (../m1-boot-speed/build-kc.nix) with the platform kext of
# ./build-platform.nix in place of ../m1-smp/build-platform.nix; everything else is the
# same build. Boots under the hypervisor exactly as the default; natively, with
# sfcpuheld=1, it can start the secondary cores.
#   nix-build build-kc.nix --no-out-link
# Boot-args and launcher that go with it: sf_quiet_wait=0 sf_rtc_wait=0 as the default, and
# ./launcher-native.patch with ./native_boot.py (tools/nb_boot.sh).
# The default collection links a stripped copy of each kext (../m1-cpufreq/build-kc.nix),
# so the platform kext is found among the collection's `-kext` paths by what the bundle
# holds (the default collection must be in the store, which it is once it has been built),
# and replaced by a copy of ours stripped the same way.
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; platform = import ./build-platform.nix; } else
let
  pins = import ../m1-rust-kext/pins.nix;
  pkgs = pins.pkgs;
  base = import ../m1-boot-speed/build-kc.nix { inherit kernelConfig; };
  strip = "${pins.crossToolchain}/bin/arm64-apple-darwin20.4-strip";
  full = import ./build-platform.nix;
  ours = pkgs.runCommand "${full.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${full}/System/Library/Extensions/SFArmPlatformExpert.kext $out/bundle.kext
    chmod -R u+w $out
    ${strip} -S $out/bundle.kext/Contents/MacOS/SFArmPlatformExpert
  '';
  named = map builtins.unsafeDiscardStringContext (builtins.concatLists (builtins.filter builtins.isList
    (builtins.split ''-kext "(/nix/store/[^"]+[.]kext)"'' base.buildPhase)));
  theirs = builtins.filter (p: builtins.pathExists (p + "/Contents/MacOS/SFArmPlatformExpert")) named;
in assert builtins.length theirs == 1;
base.overrideAttrs (old:
  let phase = builtins.replaceStrings [ ''-kext "${builtins.head theirs}"'' ] [ ''-kext "${ours}/bundle.kext"'' ] old.buildPhase;
  in assert phase != old.buildPhase; { buildPhase = phase; })
