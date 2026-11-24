# SPDX-License-Identifier: MIT
# QEMU rehearsal image for the disk-root work: ../qemu-dev/build-image.nix (unchanged) with
# ./ext4-downgrade.patch applied to its kexts' source. Hand the result to the qemu-dev tools
# with QD_BASE_IMAGE=<result>/<name>.img (tools/dr_qemu.sh does when DR_QEMU_EXT4=1).
# ext4Patch: another patch for the ext4 driver (tools/dr_qemu_massdelete.sh: the driver of before boot dr41).
{ kernelConfig ? "debug", ext4Patch ? ./ext4-downgrade.patch }:
let
  inherit (import ../rebase/pd.nix) pkgs;
  base = import ../qemu-dev/build-image.nix { inherit kernelConfig; };
  kc = import ../qemu-dev/build-kc.nix { inherit kernelConfig; };
in base.override (old: {
  kc = kc.override (k: {
    kexts = k.kexts.overrideAttrs (o: {
      src = pkgs.applyPatches { name = "siliconforest-qemu-dev-kexts-ext4-downgrade"; src = o.src; patches = [ ext4Patch ]; };
    });
  });
})
