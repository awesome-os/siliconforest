# SPDX-License-Identifier: MIT
let base = import ./build.nix;
in base.overrideAttrs (old: {
  name = "siliconforest-finch-AVFAudio-retained-hal-arm64";
  patches = (old.patches or []) ++ [ ./0003-retained-hal-output.patch ];
  postPatch = (old.postPatch or "") + ''
    cp ${./sf-engine-output.h} userland/AVFAudio/sf-engine-output.h
  '';
})
