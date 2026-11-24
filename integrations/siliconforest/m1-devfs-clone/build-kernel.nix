# SPDX-License-Identifier: MIT
# The exact qualified29 core derivation, plus only the devfs lifetime/error patch.
let
  base = import ../rebase/kernel.nix {};
  inherit (import ../rebase/pd.nix) pkgs;
in assert base.drvPath == "/nix/store/r0519s8aq63sg26vwqmsz9an7wka1crx-puredarwin-kernel-arm64-t8103-siliconforest-0.1.drv";
base.overrideAttrs (old: {
  pname = "siliconforest-kernel-t8103-devfs-clone";
  name = "siliconforest-kernel-t8103-devfs-clone-0.1";
  patches = (old.patches or []) ++ [ ./kernel.patch ];
  nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.python3 ];
  prePatch = (old.prePatch or "") + "\n" + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} kernel before
  '';
  postPatch = (old.postPatch or "") + "\n" + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} kernel_build after
  '';
})
