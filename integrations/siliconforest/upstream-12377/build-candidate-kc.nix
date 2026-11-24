# SPDX-License-Identifier: MIT
# Offline candidate; no install, boot args or hardware operation.
{ frozenPhase ? builtins.storePath "/nix/store/dblgakq73mipfll9vqnsaz4zji2sq85v-firefox29-frozen-build.sh", withNetwork ? false }:
let
  inherit (import ../rebase/pd.nix) pkgs;
  base = import ../m1-ext4-eof/build-kc.nix { inherit frozenPhase; };
  selected = import ./build-kexts.nix;
  usb = import ./build-qualified-xhci.nix {};
  baseline = builtins.storePath "/nix/store/g3ns0gjbmyri1mxglkyzj5hr3917775l-puredarwin-cio-enumeration-base";
  profile = pkgs.runCommand "siliconforest-upstream-qualified-enumeration-profile" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    dest="$out/System/Library/Extensions"
    mkdir -p "$dest"
    cp -R ${selected}/System/Library/Extensions/IOPCIFamily.kext "$dest/"
    chmod -R u+w "$dest"
    # Preserve every deployed bridge/Wi-Fi admission decision in IOPCIFamily metadata.
    cp ${baseline}/System/Library/Extensions/IOPCIFamily.kext/Contents/Info.plist "$dest/IOPCIFamily.kext/Contents/Info.plist"
    cp -R ${selected}/System/Library/Extensions/IOUSBCompositeDriver.kext "$dest/"
    cp -R ${selected}/System/Library/Extensions/SFFBSDem.kext "$dest/"
    cp -R ${selected}/System/Library/Extensions/SFFBSDigc.kext "$dest/"
    cp -R ${usb}/System/Library/Extensions/RavynXHCIPort.kext "$dest/"
    chmod -R u+w "$dest"
    python3 ${../m1-thunderbolt/tools/enumeration-usb-personalities.py} "$dest/RavynXHCIPort.kext/Contents/Info.plist"
    # These new driver modules are link/build coverage, NOT endpoint admission.
    python3 ${../m1-thunderbolt/tools/enumeration-pci-profile.py} "$dest"
  '';
  prepare = ../m1-ext4-eof/prepare-kc.py;
  verify = ../m1-ext4-eof/verify-collection.py;
  strip = ../m1-firefox-kernel/strip-kc-inputs.py;
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildCommand != builtins.stringLength (builtins.replaceStrings [ "${prepare}" ] [ "" ] old.buildCommand);
  assert builtins.stringLength old.buildCommand != builtins.stringLength (builtins.replaceStrings [ "${verify}" ] [ "" ] old.buildCommand);
{
  name = "siliconforest-kc-firefox45-selected-upstream";
  SF_SELECTED_PREPARE = prepare;
  SF_SELECTED_PROFILE = profile;
  # Both network modules exceed the frozen core's 16 MiB KC slot. Preserve the
  # failed full variant as an explicit build option, not an unsafe core move.
  SF_SELECTED_NETWORK_MODULES = if withNetwork then "1" else "0";
  SF_SELECTED_AVD = builtins.storePath "/nix/store/lc4prkcpzik8vigik57nzx9mbmnkb6s3-puredarwin-avd-context-stats-0.1";
  SF_SELECTED_BASELINE = builtins.storePath "/nix/store/f56h6c3rq1x2n29rhh0rxrds6jy8gn29-puredarwin-kc-firefox44-ext4-eof";
  SF_DEVFS_KERNEL = builtins.storePath "/nix/store/sp4zw2w9ljyxrba6qxff2avaq0crkmzz-puredarwin-kernel-t8103-devfs-upl-private-0.1";
  buildCommand = builtins.replaceStrings [ "${prepare}" "${verify}" "${strip}" ]
    [ "${./prepare-candidate-kc.py}" "${./verify-candidate.py}" "${./strip-candidate-kc-inputs.py}" ] old.buildCommand;
})
