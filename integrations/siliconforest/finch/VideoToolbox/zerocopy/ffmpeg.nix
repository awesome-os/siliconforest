# SPDX-License-Identifier: MIT
# Keep the upstream sfavd FFmpeg source/recipe, append opt-in buffer import only.
let e=import ../../environment.nix; base=import ../../../m1-avd/build-ffmpeg-sfavd.nix; in
base.overrideAttrs(old:{
 pname="siliconforest-ffmpeg-sfavd-external-capture";
 nativeBuildInputs=(old.nativeBuildInputs or [])++[e.pkgs.python3];
 postPatch=(old.postPatch or "")+''
  cp ${./capture-pool.h} libavutil/sf_sfavd_capture_pool.h
  python3 ${./ffmpeg-port.py} .
 '';
})
