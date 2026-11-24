# SPDX-License-Identifier: MIT
# Image43 cohort plus the real private-UPL core fix and reviewed AVD import.
{ frozenPhase }:
let
  base = import ../m1-firefox-kernel/build-final-kc.nix { inherit frozenPhase; };
  inherit (import ../rebase/pd.nix) pkgs;
  prepare = ../m1-firefox-kernel/prepare-final-kc.py;
  verify = ../m1-firefox-kernel/verify-final-kc.py;
  cacheVerify = ../m1-firefox-kernel/verify-baseline-avd.py;
in base.overrideAttrs (old:
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${prepare}") old.buildCommand) == 2;
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${verify}") old.buildCommand) == 2;
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${cacheVerify}") old.buildCommand) == 2;
{
  name = "siliconforest-kc-firefox29-devfs-upl-import-pinch-killable";
  SF_DEVFS_KERNEL = import ./build-kernel.nix;
  SF_UPL_PREPARE = prepare;
  SF_UPL_IMPORT_AVD = builtins.storePath "/nix/store/5238qcrsg1syilcp8h613qa6sdqn2d0d-puredarwin-firefox-avd-import-cache-0.1";
  buildCommand = builtins.replaceStrings [ "${prepare}" "${verify}" "${cacheVerify}" ]
    [ "${./prepare-kc.py}" "${./verify-collection.py}" "${../m1-firefox-kernel/verify-cache-link.py}" ] old.buildCommand;
})
