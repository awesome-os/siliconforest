# SPDX-License-Identifier: MIT
let
  base = import ../m1-avd-context-stats/build-kext.nix;
in assert base.outPath == "/nix/store/lc4prkcpzik8vigik57nzx9mbmnkb6s3-puredarwin-avd-context-stats-0.1";
base.overrideAttrs (old: {
  pname = "siliconforest-avd-capacity";
  postPatch = (old.postPatch or "") + ''
    python3 ${./apply.py} "$PWD" ${./manifest.json} ${./capacity.patch}
  '';
})
