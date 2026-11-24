# SPDX-License-Identifier: MIT
# The default desktop collection (../m1-cpufreq/build-kc.nix: eight cores, SFCpuPower) plus
# SFAppleSound.kext (./build-kext.nix), with its debug map stripped like the other kexts
# that recipe adds, and with the debug maps of the base members stripped as well (see below).
# sfsound=0 in the boot-args leaves the audio hardware untouched.
#   nix-build ... build-kc.nix --argstr kernelConfig development
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; } else
let
  pins = import ../m1-rust-kext/pins.nix;
  pkgs = pins.pkgs;
  base = import ../m1-cpufreq/build-kc.nix { inherit kernelConfig; };
  strip = "${pins.crossToolchain}/bin/arm64-apple-darwin20.4-strip";
  full = import ./build-kext.nix;
  kext = pkgs.runCommand "${full.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${full}/System/Library/Extensions/SFAppleSound.kext $out/bundle.kext
    chmod -R u+w $out
    ${strip} -S $out/bundle.kext/Contents/MacOS/SFAppleSound
  '';
  needle = "-o kernel";
  # The base members the recipe takes from one directory ($KEXTS) still carry their debug
  # maps. The same `strip -S` on every one of them frees about 60 pages (1 MiB) of the
  # 16 MiB between the collection header and the kernel; no member is removed or changed
  # otherwise. Without it this kext no longer fits (__PRELINK_INFO ran into __TEXT).
  kextsLine = builtins.head (builtins.match ".*\nKEXTS=(/nix/store/[^\n]+)\n.*" base.buildPhase);
  kextsDir = builtins.unsafeDiscardStringContext kextsLine;
  kextsStore = builtins.head (builtins.match "(/nix/store/[^/]+).*" kextsDir);
  kextsInside = builtins.substring (builtins.stringLength kextsStore) (builtins.stringLength kextsDir) kextsDir;
  stripped = pkgs.runCommand "kexts-no-debug-map" { } ''
    mkdir -p $out
    cp -RL ${builtins.storePath kextsStore}${kextsInside}/. $out/
    chmod -R u+w $out
    for b in $out/*.kext/Contents/MacOS/*; do
      ${strip} -S "$b"
    done
  '';
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle "KEXTS=${kextsDir}" ] [ ''-kext "${kext}/bundle.kext" \
      -o kernel'' "KEXTS=${stripped}" ] old.buildPhase;
  })
