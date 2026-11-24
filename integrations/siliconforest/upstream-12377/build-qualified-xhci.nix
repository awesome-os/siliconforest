# SPDX-License-Identifier: MIT
# Preserve the deployed CIO29/45 DART/PHY/lifetime implementation; only port USB.
{ port ? true }:
let
  base = import ../m1-usb-dwc3/build-host.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/0rcjmq9k28jnn0y0qvzxvqm2nhblkbx7-puredarwin-apple-usb-host-tree";
in base.overrideAttrs (old: {
  pname = if port then "siliconforest-qualified-xhci-upstream-usb" else "siliconforest-qualified-xhci-reproduce";
  src = frozen;
  patches = (old.patches or []) ++ pkgs.lib.optionals port [ ./patches/aaf3604d-usb.patch ];
})
