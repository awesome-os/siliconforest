# SPDX-License-Identifier: MIT
# Dedicated source-only PCI dock variant. Never selected by the daily KC.
let
 source=import ../rebase/pd.nix;
 inherit (source) pkgs;
 base=import ./build-dock-usb-dma-objects.nix;
in base.overrideAttrs (old: {
 pname="siliconforest-dock-fl1100";
 src=pkgs.runCommand "siliconforest-dock-fl1100-tree" {nativeBuildInputs=[pkgs.python3];} ''
  cp -R ${old.src} "$out"
  chmod -R u+w "$out"
  python3 ${./tools/prepare_dock_usb.py} "$out"
 '';
 installPhase=''
  mkdir -p "$out/System/Library/Extensions"
  cp -R build-nix/src/Kernel/Extensions/RavynXHCIPort/RavynXHCIPort.kext "$out/System/Library/Extensions/"
  cp ${../m1-usb-dwc3/NOTICE} "$out/System/Library/Extensions/RavynXHCIPort.kext/Contents/NOTICE"
  chmod u+w "$out/System/Library/Extensions/RavynXHCIPort.kext/Contents/NOTICE"
  cat ${../m1-atcphy/NOTICE} >> "$out/System/Library/Extensions/RavynXHCIPort.kext/Contents/NOTICE"
 '';
})
