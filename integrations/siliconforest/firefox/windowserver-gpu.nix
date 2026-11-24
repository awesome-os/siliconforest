# SPDX-License-Identifier: MIT
# Private provider only; never installs over the Cocoa owner's WindowServer.
let
 a=import ../cocoa/arm64.nix;
 p=import ../cocoa/patched.nix;
 src=a.pkgs.runCommand "sffx-windowserver-gpu-source" {} ''
  cp -r ${p.windowserverSrc} $out
  chmod -R u+w $out
  cp ${./gpu-present.h} $out/sffx_gpu_present.h
  cd $out
  ${a.pkgs.patch}/bin/patch --batch -p1 < ${./windowserver-patches/0001-opt-in-whole-window-gpu-layer-presentation.patch}
 '';
in p.windowserver.override {inherit src;}
