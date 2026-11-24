# SPDX-License-Identifier: MIT
# a08eb9b6's real trustd header; omit its git arguments, reverted by 63a61e89.
let
  inherit (import ../rebase/pd.nix) pd;
in pd.packages.x86_64-linux.security-arm64.overrideAttrs (old: {
  pname = "siliconforest-security-upstream-trustd-header";
  patches = (old.patches or []) ++ [ ./patches/a08eb9b6-trustd-header.patch ];
})
