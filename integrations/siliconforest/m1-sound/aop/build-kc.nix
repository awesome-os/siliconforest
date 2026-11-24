# SPDX-License-Identifier: MIT
# The sound collection (../build-kc.nix: the default desktop collection plus SFAppleSound,
# debug maps stripped) plus SFAopAudio.kext (./build-kext.nix), appended the same way and
# with its debug map stripped as well. sfaop=0 in the boot-args leaves the AOP and the
# microphones untouched.
#   nix-build ... build-kc.nix --argstr kernelConfig development
# The build prints the collection's layout check: look for "0 overlapping pair(s)".
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../../rebase/pd.nix), where this is
# ../../rebase/build-kc.nix with one more member; what follows builds the pinned revision and is
# reached with SF_PD=pin.
if (import ../../rebase/pd.nix).tip then import ../../rebase/build-kc.nix { inherit kernelConfig; more = [ { name = "SFAopAudio"; from = import ./build-kext.nix; } ]; } else
let
  pins = import ../../m1-rust-kext/pins.nix;
  pkgs = pins.pkgs;
  base = import ../build-kc.nix { inherit kernelConfig; };
  strip = "${pins.crossToolchain}/bin/arm64-apple-darwin20.4-strip";
  full = import ./build-kext.nix;
  kext = pkgs.runCommand "${full.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${full}/System/Library/Extensions/SFAopAudio.kext $out/bundle.kext
    chmod -R u+w $out
    ${strip} -S $out/bundle.kext/Contents/MacOS/SFAopAudio
  '';
  needle = "-o kernel";
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''-kext "${kext}/bundle.kext" \
      -o kernel'' ] old.buildPhase;
  })
