# SPDX-License-Identifier: MIT
# Qualified29 core + the qualified devfs fix + the private-UPL fast-path exclusion.
let
  base = import ../m1-devfs-clone/build-kernel.nix;
  inherit (import ../rebase/pd.nix) pkgs;
in base.overrideAttrs (old: {
  pname = "siliconforest-kernel-t8103-devfs-upl-private";
  name = "siliconforest-kernel-t8103-devfs-upl-private-0.1";
  patches = (old.patches or []) ++ [ ./kernel.patch ];
  prePatch = (old.prePatch or "") + "\n" + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} before
  '';
  postPatch = (old.postPatch or "") + "\n" + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} after
  '';
})
