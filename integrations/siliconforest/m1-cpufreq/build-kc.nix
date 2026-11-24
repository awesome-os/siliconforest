# SPDX-License-Identifier: MIT
# The desktop collection on eight cores (../m1-smp/build-kc-desktop.nix) plus
# SFCpuPower.kext (./build-kext.nix). The kext does nothing on a chip other than T8103;
# sfcpufreq=0 sfthermal=0 in the boot-args leaves the clusters as before it existed.
#
# Room: the desktop collection had less than one kext's worth of space left below the
# kernel's __TEXT (kc_overlap.py: with this kext added, __PRELINK_INFO ran 0x2375c bytes
# into it), and the launcher's image gate wants every one of its 50 base members present,
# so none can be left out. What ../m1-dcp/build-kc.nix does for SFDcp and SFAgx (`slim`)
# is done here for every kext the recipe names by store path (Wi-Fi, the PCIe bridge,
# SFGpuDT, the SPI keyboard, the platform expert, this one): `strip -S`, which removes the
# debug map (N_OSO/N_FUN stabs) from __LINKEDIT and nothing else. That frees 0x34000 bytes.
#
# `--arg desktop <path>`: another copy of the desktop recipe, e.g. the one of a committed
# tree (git archive HEAD integrations/siliconforest | tar -x -C <dir>) while ../m1-smp is
# being worked on by someone else.
{ kernelConfig ? "debug", desktop ? ../m1-smp/build-kc-desktop.nix }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; } else
let
  pins = import ../m1-rust-kext/pins.nix;
  pkgs = pins.pkgs;
  recipe = import desktop;
  # kernelConfig is passed on only to a recipe that knows it (the committed one may not yet)
  base = recipe (if (builtins.functionArgs recipe) ? kernelConfig then { inherit kernelConfig; } else { });
  strip = "${pins.crossToolchain}/bin/arm64-apple-darwin20.4-strip";
  full = import ./build-kext.nix;
  kext = pkgs.runCommand "${full.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${full}/System/Library/Extensions/SFCpuPower.kext $out/bundle.kext
    chmod -R u+w $out
    ${strip} -S $out/bundle.kext/Contents/MacOS/SFCpuPower
  '';
  # every `-kext "/nix/store/....kext"` of the base recipe: built already, named by path
  named = builtins.concatLists (builtins.filter builtins.isList
    (builtins.split ''-kext "(/nix/store/[^"]+[.]kext)"'' base.buildPhase));
  noDebugMap = path:
    let
      storeDir = builtins.head (builtins.match "(/nix/store/[^/]+).*" path);
      inside = builtins.substring (builtins.stringLength storeDir) (builtins.stringLength path) path;
      input = builtins.storePath storeDir;
    in pkgs.runCommand "kext-no-debug-map" { } ''
      mkdir -p $out
      cp -R ${input}${inside} $out/bundle.kext
      chmod -R u+w $out
      for b in $out/bundle.kext/Contents/MacOS/*; do
        ${strip} -S "$b"
      done
    '';
  paths = map builtins.unsafeDiscardStringContext named;
  slimmed = builtins.replaceStrings (map (p: ''-kext "${p}"'') paths)
    (map (p: ''-kext "${noDebugMap p}/bundle.kext"'') paths) base.buildPhase;
  needle = "-o kernel";
in base.overrideAttrs (old:
  assert builtins.stringLength slimmed
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] slimmed);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''-kext "${kext}/bundle.kext" \
      -o kernel'' ] slimmed;
  })
