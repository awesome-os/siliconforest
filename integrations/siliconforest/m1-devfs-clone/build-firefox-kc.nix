# SPDX-License-Identifier: MIT
# Highest-priority devfs correction in the SAME future Firefox/K14/HID install.
{ frozenPhase }:
let
  base = import ../m1-firefox-kernel/build-kc.nix {
    inherit frozenPhase;
    hid = builtins.storePath "/nix/store/zc02mxv57xr0snv68pzdl5pynkidmd6i-puredarwin-firefox-native-hid-readiness-0.1";
    spi = builtins.storePath "/nix/store/42j1wmvkbyfmj0hvq5m9d9s0v5xazg39-puredarwin-firefox-k14-touchbar-0.1";
    avd = builtins.storePath "/nix/store/5238qcrsg1syilcp8h613qa6sdqn2d0d-puredarwin-firefox-avd-import-cache-0.1";
  };
  inherit (import ../rebase/pd.nix) pkgs;
  core = import ./build-kernel.nix;
  dri = import ./build-dri.nix;
  oldPrepare = ../m1-firefox-kernel/prepare-kc.py;
  oldVerify = ../m1-firefox-kernel/verify-collection.py;
in base.overrideAttrs (old:
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${oldPrepare}") old.buildCommand) == 2;
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${oldVerify}") old.buildCommand) == 2;
{
  name = "siliconforest-kc-firefox29-devfs-import-pinch-readiness";
  SF_FIREFOX_PREPARE = oldPrepare;
  SF_FIREFOX_CACHE_VERIFY = ../m1-firefox-kernel/verify-cache-link.py;
  SF_DEVFS_KERNEL = core;
  SF_DEVFS_DRI = dri;
  buildCommand = builtins.replaceStrings [ "${oldPrepare}" "${oldVerify}" ]
    [ "${./prepare-firefox-kc.py}" "${./verify-firefox-collection.py}" ] old.buildCommand + ''
      python3 ${./verify-dri-link.py} kernel > "$out/nix-support/devfs-api-link-proof.json"
    '';
})
