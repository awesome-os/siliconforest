# SPDX-License-Identifier: MIT
# Dedicated USB-storage qualification SDK. Does not enter any boot profile.
# This adds write rejection to the existing USB lifetime variant; it DOES
# NOT provide the PCIe-C shared SID0 mapper, MSI or endpoint admission.
let
 source=import ../rebase/pd.nix;
 inherit (source) pkgs;
 base=import ../m1-usb-dwc3/build-host.nix;
in base.overrideAttrs (old: {
 pname="siliconforest-dock-xhci-readonly";
 src=pkgs.runCommand "siliconforest-dock-xhci-readonly-tree" {nativeBuildInputs=[pkgs.patch];} ''
  cp -R ${old.src} "$out"
  chmod -R u+w "$out"
  cd "$out"
  patch --fuzz=0 -p1 < ${./patches/dock-usb-readonly.patch}
 '';
})
