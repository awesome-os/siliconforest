# SPDX-License-Identifier: MIT
# The ext4.kext of kernel 48/49 (m1-kernel48/build-kc.nix: clean downgrade, end-of-file fix,
# journal read cache), and with `fixed` the same plus ./fsck-clean.patch.
#   nix-build build-ext4.nix [--arg fixed false] --no-out-link
{ fixed ? true }:
let
  k48 = (import ../m1-kernel48/build-kc.nix).ext4;
in if !fixed then k48 else k48.overrideAttrs (old: {
  pname = "siliconforest-ext4-fsck-clean";
  name = "siliconforest-ext4-fsck-clean-0.1";
  patches = (old.patches or [ ]) ++ [ ./fsck-clean.patch ];
})
