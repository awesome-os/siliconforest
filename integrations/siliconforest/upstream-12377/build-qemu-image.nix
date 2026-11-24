# SPDX-License-Identifier: MIT
# Isolated PCI/USB/network rehearsal; the M1 collection is a separate artifact.
let
  inherit (import ../rebase/pd.nix) pkgs;
  base = import ../qemu-dev/build-image.nix { kernelConfig = "debug"; };
  kc = import ../qemu-dev/build-kc.nix { kernelConfig = "debug"; };
  selected = import ./build-kexts.nix;
  ext4 = import ../m1-ext4-eof/build-ext4.nix;
in base.override (old: {
  kc = kc.override (k: {
    kexts = pkgs.runCommand "siliconforest-upstream-selected-qemu-kexts" {} ''
      mkdir -p "$out"
      cp -rs ${k.kexts}/. "$out/"
      find "$out" -type d -exec chmod u+w {} +
      for name in IOPCIFamily IOUSBCompositeDriver RavynXHCIPort SFFBSDem SFFBSDigc; do
        dest="$out/System/Library/Extensions/$name.kext"
        rm -rf "$dest"
        ln -s ${selected}/System/Library/Extensions/$name.kext "$dest"
      done
      rm -rf "$out/System/Library/Extensions/ext4.kext"
      ln -s ${ext4}/System/Library/Extensions/ext4.kext "$out/System/Library/Extensions/ext4.kext"
    '';
  });
})
