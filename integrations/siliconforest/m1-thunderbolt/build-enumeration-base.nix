# SPDX-License-Identifier: MIT
# Require an explicit frozen input. Copy/adjust metadata only, no driver build,
# installed-file edit or default-collection change. Native CIO also verifies
# actual IOCatalogue contents before authorizing a PCIe tunnel.
{ kexts }:
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs;
in pkgs.runCommand "siliconforest-cio-enumeration-base" {
  nativeBuildInputs = [ pkgs.python3 ];
} ''
  mkdir -p "$out"
  cp -R ${kexts}/. "$out/"
  chmod -R u+w "$out"
  mkdir -p "$out/nix-support"
  python3 ${./tools/enumeration-pci-profile.py} "$out/System/Library/Extensions" > "$out/nix-support/pci-profile.json"
''
