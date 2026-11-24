# SPDX-License-Identifier: MIT
# Exact deployed45/46/47 ext4 and EOF fix; only journal read-cache optimization.
let
  base = import ../m1-ext4-eof/build-ext4.nix;
  inherit (import ../rebase/pd.nix) pkgs;
in assert base.outPath == "/nix/store/ar0bc55waldpnvm7vc8sgqm9r5f7glw7-puredarwin-ext4-eof-0.1";
base.overrideAttrs (old: {
  pname = "siliconforest-ext4-journal-cache";
  name = "siliconforest-ext4-journal-cache-0.1";
  patches = (old.patches or []) ++ [ ./journal-cache.patch ];
})
