# SPDX-License-Identifier: MIT
# Actual PM bundle links/boots in virt, but no emulated Broadcom firmware exists.
let
  inherit (import ../rebase/pd.nix) pkgs;
  base = import ../qemu-dev/build-image.nix { kernelConfig = "debug"; };
  kc = import ../qemu-dev/build-kc.nix { kernelConfig = "debug"; };
  selected = import ../upstream-12377/build-kexts.nix;
  ext4 = import ../m1-ext4-eof/build-ext4.nix;
  wifi = import ./build-driver.nix {};
  frozen = builtins.storePath "/nix/store/qqm87ypjypfsm563a9yz94ky01kqnywa-puredarwin-bwfm-0.1";
in base.override (old: {
  kc = kc.override (k: {
    extraKexts = (k.extraKexts or []) ++ [
      "${frozen}/System/Library/Extensions/IO80211Family.kext"
      "${wifi}/System/Library/Extensions/SFBwfm.kext"
    ];
    kexts = pkgs.runCommand "siliconforest-wifi-pm-qemu-kexts" {} ''
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
