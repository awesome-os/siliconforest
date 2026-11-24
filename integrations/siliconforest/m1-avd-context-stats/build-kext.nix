# SPDX-License-Identifier: MIT
let
  base = import ../m1-firefox-kernel/build-avd-import.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/yw38khclrlqp2a7hric4i314ckcjs2in-puredarwin-firefox-avd-import-tree";
in assert base.outPath == "/nix/store/5238qcrsg1syilcp8h613qa6sdqn2d0d-puredarwin-firefox-avd-import-cache-0.1";
base.overrideAttrs (old: {
  pname = "siliconforest-avd-context-stats";
  src = frozen;
  patches = (old.patches or []) ++ [ ./context-stats.patch ];
  prePatch = (old.prePatch or "") + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} before
  '';
  postPatch = (old.postPatch or "") + ''
    python3 ${./verify-source.py} "$PWD" ${./manifest.json} after
  '';
  installPhase = (old.installPhase or "") + ''
    mkdir -p "$out/include/sfavd" "$out/nix-support"
    cp src/Kernel/Extensions/SFAppleAVD/include/sfavd/sfavd_ioctl.h "$out/include/sfavd/"
    cp ${./manifest.json} "$out/nix-support/context-stats-source-manifest.json"
    cp ${./context-stats.patch} "$out/nix-support/context-stats.patch"
  '';
})
