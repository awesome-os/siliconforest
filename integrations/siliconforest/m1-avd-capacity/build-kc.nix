# SPDX-License-Identifier: MIT
let
  base = import ../m1-ext4-journal-cache/build-kc.nix;
  verify = ../m1-ext4-journal-cache/verify-kc.py;
in base.overrideAttrs (old: {
  name = "siliconforest-kc-firefox49-avd-capacity";
  SF_SELECTED_AVD = import ./build-kext.nix;
  SF_WIFI_BASELINE = builtins.storePath "/nix/store/bad0wz2p2nhsi2j261z2lldc4jmwkqg2-puredarwin-kc-firefox48-ext4-journal-cache";
  buildCommand = builtins.replaceStrings [ "${verify}" ] [ "${./verify-kc.py}" ] old.buildCommand;
})
