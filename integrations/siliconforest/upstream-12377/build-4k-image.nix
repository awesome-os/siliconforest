# SPDX-License-Identifier: MIT
# Offline regular-image conversion rehearsal. This is NOT an M1 disk installer.
let
  inherit (import ../rebase/pd.nix) pd pkgs;
  image = import ./build-qemu-image.nix;
  converter = pkgs.runCommand "siliconforest-upstream-gpt4k-source" { nativeBuildInputs = [ pkgs.patch ]; } ''
    mkdir -p "$out/nix/lib"
    cd "$out"
    patch --fuzz=0 -p1 < ${./patches/aa82116c-gpt4k.patch}
  '';
in pkgs.runCommand "siliconforest-upstream-4k-regular-image" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  mkdir -p "$out"
  cp --sparse=always ${image}/*.img "$out/virt-rehearsal-4k.img"
  chmod u+w "$out/virt-rehearsal-4k.img"
  python3 ${converter}/nix/lib/gpt4k.py "$out/virt-rehearsal-4k.img"
  # The kernel in this rehearsal image is virt, not T8103. Never deploy it.
''
