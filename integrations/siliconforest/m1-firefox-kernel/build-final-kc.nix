# SPDX-License-Identifier: MIT
# Retain devfs/K14/HID, add reviewed killable waits and optional speaker cap default OFF; exclude failed AVD import.
{ frozenPhase }:
let
  base = import ../m1-devfs-clone/build-firefox-kc.nix { inherit frozenPhase; };
  inherit (import ../rebase/pd.nix) pkgs;
  prepare = ../m1-devfs-clone/prepare-firefox-kc.py;
  verify = ../m1-devfs-clone/verify-firefox-collection.py;
  cacheVerify = ./verify-cache-link.py;
  oldAvd = builtins.storePath "/nix/store/98npxmswr4rbvwm4x2b6898qc644mnxr-PDAppleAVD-no-debug-map";
  agx = builtins.storePath "/nix/store/cg6gkwr9l2jlpsadyli0xrhdcxvr3dv1-puredarwin-agx-killable-0.1";
  dcp = builtins.storePath "/nix/store/2mcyk55yv2mvmq0l4p2rcd2f51465hp6-puredarwin-dcp-killable-0.1";
  sound = builtins.storePath "/nix/store/c5zjb6pd7pjr1ay1gbdf1drvgs0fdhc6-puredarwin-sound-0.1";
in base.overrideAttrs (old:
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${prepare}") old.buildCommand) == 2;
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${verify}") old.buildCommand) == 2;
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${cacheVerify}") old.buildCommand) == 2;
{
  name = "siliconforest-kc-firefox29-devfs-pinch-killable-speaker-cap";
  SF_FINAL_BASELINE_AVD = oldAvd;
  SF_COMBINED_PREPARE = prepare;
  SF_FINAL_AGX = agx;
  SF_FINAL_DCP = dcp;
  SF_FINAL_SOUND = sound;
  buildCommand = builtins.replaceStrings [ "${prepare}" "${verify}" "${cacheVerify}" ]
    [ "${./prepare-final-kc.py}" "${./verify-final-kc.py}" "${./verify-baseline-avd.py}" ] old.buildCommand;
})
