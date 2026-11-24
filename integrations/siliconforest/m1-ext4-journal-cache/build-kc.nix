# SPDX-License-Identifier: MIT
# Qualified47; replace only ext4, retain the Wi-Fi fix and identical core.
let
  base = import ../m1-wifi-pm/build-kc.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  verify = ../m1-wifi-pm/verify-kc.py;
in base.overrideAttrs (old:
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${verify}") old.buildCommand) == 2;
{
  name = "siliconforest-kc-firefox48-ext4-journal-cache";
  SF_EOF_EXT4 = import ./build-ext4.nix;
  SF_WIFI_BASELINE = builtins.storePath "/nix/store/lmlv70qvz3mpcya6d19wlpqn8p1ck165-puredarwin-kc-firefox47-wifi-pm";
  buildCommand = builtins.replaceStrings [ "${verify}" ] [ "${./verify-kc.py}" ] old.buildCommand;
})
