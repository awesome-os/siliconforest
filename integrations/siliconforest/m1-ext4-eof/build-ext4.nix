# SPDX-License-Identifier: MIT
# Exact qualified29/44 ext4 input, one allocation-limit change only.
let
  base = import ../m1-storage/build-ext4.nix;
  inherit (import ../rebase/pd.nix) pkgs;
in assert base.outPath == "/nix/store/11f479qxfnb83zhnr60fgj5bk4f2d1s1-puredarwin-ext4-downgrade-0.1";
base.overrideAttrs (old: {
  pname = "siliconforest-ext4-eof";
  name = "siliconforest-ext4-eof-0.1";
  patches = (old.patches or []) ++ [ ./eof.patch ];
  nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.python3 ];
  prePatch = (old.prePatch or "") + "\n" + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} before
  '';
  postPatch = (old.postPatch or "") + "\n" + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} after
  '';
})
