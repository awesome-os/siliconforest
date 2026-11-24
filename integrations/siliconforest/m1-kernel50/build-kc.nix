# SPDX-License-Identifier: MIT
# Kernel 50: kernel 48/49 (../m1-kernel48/build-kc.nix, unchanged) with ONE member replaced:
# ext4.kext built with ../m1-ext4-fsck-clean/fsck-clean.patch on top of kernel 48's three ext4
# patches. The kernel, the other 55 kexts, the link phase and its checks are kernel 48's.
#   nix-build build-kc.nix --no-out-link                 the collection ($out/kernel)
#   nix-build build-kc.nix -A ext4 --no-out-link         the one changed member
let
  k48 = import ../m1-kernel48/build-kc.nix;
  ext = "System/Library/Extensions";
  ext4 = import ../m1-ext4-fsck-clean/build-ext4.nix { fixed = true; };
  old = "${k48.ext4}/${ext}/ext4.kext";
  new = "${ext4}/${ext}/ext4.kext";
  collection = k48.override (o: {
    extraKexts = assert builtins.elem old o.extraKexts; map (k: if k == old then new else k) o.extraKexts;
  });
in collection.overrideAttrs (o: {
  name = "siliconforest-kc-kernel50";
  passthru = (o.passthru or { }) // { inherit ext4; kernel48 = k48; };
})
