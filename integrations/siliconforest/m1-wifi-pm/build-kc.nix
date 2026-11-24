# SPDX-License-Identifier: MIT
# Qualified46 with only SFBwfm replaced. Boot arguments are kept separately.
let
  base = import ../upstream-12377/build-candidate-kc.nix {};
  inherit (import ../rebase/pd.nix) pkgs;
  prepare = ../upstream-12377/prepare-candidate-kc.py;
  verify = ../upstream-12377/verify-candidate.py;
in base.overrideAttrs (old:
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${prepare}") old.buildCommand) == 2;
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${verify}") old.buildCommand) == 2;
{
  name = "siliconforest-kc-firefox47-wifi-pm";
  SF_WIFI_PREPARE = prepare;
  SF_WIFI_DRIVER = import ./build-driver.nix {};
  SF_WIFI_BASELINE = builtins.storePath "/nix/store/379skjhjy18lbqin28lgv164wixzfjn9-puredarwin-kc-firefox45-selected-upstream";
  buildCommand = builtins.replaceStrings [ "${prepare}" "${verify}" ]
    [ "${./prepare-kc.py}" "${./verify-kc.py}" ] old.buildCommand;
})
