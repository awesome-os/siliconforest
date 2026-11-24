# Separate KC composition; root stages it after the platform bundle is ready.
# This does not build or choose a bootloader, ramdisk, init, or hardware runner.
{ pkgs, kcTools, kernel, kernelSource, genericKexts, platformKexts, kcRecipe }:
let
  # Reuse pinned generic arm64 kexts while explicitly replacing the platform
  # bundle. Missing inputs fail the derivation; no generic PE fallback.
  kexts = pkgs.runCommand "puredarwin-m1-bringup-kext-selection" { } ''
    set -eu
    dest="$out/System/Library/Extensions"
    mkdir -p "$dest"
    for bundle in ${genericKexts}/System/Library/Extensions/*.kext; do
      name=$(basename "$bundle")
      if [ "$name" != PDArmPlatformExpert.kext ]; then
        ln -s "$bundle" "$dest/$name"
      fi
    done
    platform=${platformKexts}/System/Library/Extensions/PDArmPlatformExpert.kext
    test -f "$platform/Contents/MacOS/PDArmPlatformExpert"
    ln -s "$platform" "$dest/PDArmPlatformExpert.kext"
  '';
in
# kcRecipe must be ./nix/pkgs/toolchain/kc-arm64.nix in the exact checkout.
# It is explicit because filtered kernelSource need not contain Nix recipes.
# kernel must be the native DEBUG T8103 build, verified separately by root.
pkgs.callPackage kcRecipe {
  inherit kcTools kernel kernelSource kexts;
  kcBase = "fffffe0006000000";
}
