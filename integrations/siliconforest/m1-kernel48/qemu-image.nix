# SPDX-License-Identifier: MIT
# What of ./build-kc.nix can be started in QEMU. The collection itself is a T8103 kernel, started
# by m1n1 on the machine; QEMU's `virt` board starts upstream's `virt` kernel through the EFI
# loader. Two images of the QEMU desktop loop's base (../rebase/qemu-image.nix):
#   which = "members"     (default) the `virt` collection with the members of ./build-kc.nix
#                         that are not specific to the M1 in place of its own: ext4 (clean
#                         downgrade, end-of-file fix, journal read cache), IOPCIFamily and
#                         IOUSBCompositeDriver and the xHCI driver with upstream's scan and USB
#                         changes. The root file system of this image is ext4.
#   which = "collection"  the M1 collection as the image's collection, to record what QEMU
#                         shows for it (it is not expected to start).
#   nix-build qemu-image.nix [--argstr which collection] --no-out-link
{ which ? "members" }:
let
  inherit (import ../rebase/pd.nix) pkgs;
  base = import ../rebase/qemu-image.nix { };
  m1 = import ./build-kc.nix;
  ext = "System/Library/Extensions";
in base.override (old: {
  kc = if which == "collection" then m1 else base.kc.override (k: {
    kexts = pkgs.runCommand "siliconforest-kernel48-members-qemu-kexts" { } ''
      mkdir -p "$out"
      cp -rs ${k.kexts}/. "$out/"
      find "$out" -type d -exec chmod u+w {} +
      for name in IOPCIFamily IOUSBCompositeDriver RavynXHCIPort; do
        rm -rf "$out/${ext}/$name.kext"
        ln -s ${m1.selected}/${ext}/$name.kext "$out/${ext}/$name.kext"
      done
      rm -rf "$out/${ext}/ext4.kext"
      ln -s ${m1.ext4}/${ext}/ext4.kext "$out/${ext}/ext4.kext"
    '';
  });
})
