# SPDX-License-Identifier: MIT
# Qualified44, replacing only ext4 after the isolated filesystem rehearsal.
{ frozenPhase }:
let
  base = import ../m1-upl-private/build-kc.nix { inherit frozenPhase; };
  inherit (import ../rebase/pd.nix) pkgs;
  prepare = ../m1-upl-private/prepare-kc.py;
  verify = ../m1-upl-private/verify-collection.py;
in base.overrideAttrs (old:
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${prepare}") old.buildCommand) == 2;
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${verify}") old.buildCommand) == 2;
{
  name = "siliconforest-kc-firefox44-ext4-eof";
  SF_EOF_PREPARE = prepare;
  SF_EOF_EXT4 = import ./build-ext4.nix;
  SF_EOF_BASELINE_KC = builtins.storePath "/nix/store/lss4s0dwx36q714czlpzg9hdqd8bc1i7-puredarwin-kc-firefox29-devfs-upl-import-pinch-killable";
  buildCommand = builtins.replaceStrings [ "${prepare}" "${verify}" ]
    [ "${./prepare-kc.py}" "${./verify-collection.py}" ] old.buildCommand;
})
