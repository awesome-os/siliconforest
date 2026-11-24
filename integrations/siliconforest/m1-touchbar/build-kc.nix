# SPDX-License-Identifier: MIT
# The proposed desktop collection with sound (../m1-sound/build-kc-desktop.nix: quiet-boot
# kernel, eight cores, Wi-Fi, GPU, display, sound) plus SFAppleTouchBar.kext
# (./build-kext.nix) with its debug map stripped, as that recipe does for SFAppleSound.
# The built-in keyboard's driver in it is replaced by the build of ./build-spihid.nix, which
# tells the Touch Bar about the Fn key and typing. sftouchbar=0 in the boot-args leaves the
# Touch Bar's hardware untouched. No default collection is changed by this file.
#   nix-build ... build-kc.nix --argstr kernelConfig development --no-out-link
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; } else
let
  pins = import ../m1-rust-kext/pins.nix;
  pkgs = pins.pkgs;
  base = import ../m1-sound/build-kc-desktop.nix { inherit kernelConfig; };
  strip = "${pins.crossToolchain}/bin/arm64-apple-darwin20.4-strip";
  slim = name: full: pkgs.runCommand "${full.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${full}/System/Library/Extensions/${name}.kext $out/bundle.kext
    chmod -R u+w $out
    ${strip} -S $out/bundle.kext/Contents/MacOS/${name}
  '';
  kext = slim "SFAppleTouchBar" (import ./build-kext.nix);
  spihid = slim "SFM1SPIHID" (import ./build-spihid.nix);
  needle = "-o kernel";
  # The base recipe names the keyboard's bundle by the store path of a debug-map-stripped
  # copy (../m1-cpufreq/build-kc.nix). It is found among the bundles on the link line by
  # what it contains, which needs the base collection's inputs built (build
  # ../m1-sound/build-kc-desktop.nix first).
  named = builtins.concatLists (builtins.filter builtins.isList
    (builtins.split ''-kext "(/nix/store/[^"]+[.]kext)"'' base.buildPhase));
  olds = builtins.filter (p: builtins.pathExists (p + "/Contents/MacOS/SFM1SPIHID"))
    (map builtins.unsafeDiscardStringContext named);
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  assert builtins.length olds == 1;
  {
    buildPhase = builtins.replaceStrings [ ''-kext "${builtins.head olds}"'' needle ]
      [ ''-kext "${spihid}/bundle.kext"'' ''-kext "${kext}/bundle.kext" \
      -o kernel'' ] old.buildPhase;
  })
