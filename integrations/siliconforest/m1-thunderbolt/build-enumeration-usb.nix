# SPDX-License-Identifier: MIT
# Explicit private-profile input: ordinary USB builds and installed bundles
# are never edited. This removes PCI endpoint matching, not the native PHY
# broker or its DWC3 personality. It is not endpoint DMA or tunnel admission.
{ bundle ? "${import ../m1-usb-dwc3/build-host.nix}/System/Library/Extensions/RavynXHCIPort.kext" }:
let
  source = import ../rebase/pd.nix;
  inherit (source) pkgs;
in pkgs.runCommand "siliconforest-cio-enumeration-usb" {
  nativeBuildInputs = [ pkgs.python3 ];
} ''
  source_bundle=${pkgs.lib.escapeShellArg bundle}
  dest="$out/System/Library/Extensions/RavynXHCIPort.kext"
  mkdir -p "$(dirname "$dest")" "$out/nix-support"
  cp -R "$source_bundle" "$dest"
  chmod -R u+w "$dest"
  python3 ${./tools/enumeration-usb-personalities.py} "$dest/Contents/Info.plist" > "$out/nix-support/personality-profile.json"
  # The code image must be byte-identical; this profile changes only matching.
  cmp "$source_bundle/Contents/MacOS/RavynXHCIPort" "$dest/Contents/MacOS/RavynXHCIPort"
''
