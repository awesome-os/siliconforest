# SPDX-License-Identifier: MIT
# Isolated `virt` image for run-qemu.py: the QEMU collection with only ext4.kext replaced by
# kernel 48's (fixed = false) or by the fixed one (./build-ext4.nix).
{ fixed ? true }:
let
  inherit (import ../rebase/pd.nix) pkgs;
  base = import ../qemu-dev/build-image.nix { kernelConfig = "debug"; };
  kc = import ../qemu-dev/build-kc.nix { kernelConfig = "debug"; };
  ext4 = import ./build-ext4.nix { inherit fixed; };
  ext = "System/Library/Extensions";
in base.override (old: {
  kc = kc.override (k: {
    kexts = pkgs.runCommand (if fixed then "qemu-fsck-clean-fixed-kexts" else "qemu-fsck-clean-kernel48-kexts") { } ''
      mkdir -p "$out"
      cp -rs ${k.kexts}/. "$out/"
      test ! -L "$out/System" && test ! -L "$out/System/Library" && test ! -L "$out/${ext}"
      find "$out" -type d -exec chmod u+w {} +
      test -f "$out/${ext}/ext4.kext/Contents/MacOS/ext4"
      rm -rf "$out/${ext}/ext4.kext"
      ln -s ${ext4}/${ext}/ext4.kext "$out/${ext}/ext4.kext"
    '';
  });
})
