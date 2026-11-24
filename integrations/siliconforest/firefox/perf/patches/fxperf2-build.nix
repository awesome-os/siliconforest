# SPDX-License-Identifier: MIT
# FXPERF2: Firefox's parent frameworks as the main Cocoa series builds them (cocoa/patched.nix), with or without the
# damage-only frame patches 0300-0303, packaged for a private DYLD prefix (own Firefox instance only):
#   <out>/Frameworks/{QuartzCore,AppKit,OpenGL}.framework, <out>/lib/libWindowServer.dylib
#   nix-build fxperf2-build.nix --arg damage true    (candidate)    --arg damage false (series baseline)
# series: the cocoa directory whose patch lists give the sources (default this tree's; FXPERF2 passes an export of
# the committed HEAD so builds do not follow uncommitted series work)
# qcOptimised: QuartzCore compiled -O2 -fno-strict-aliasing as the other frameworks are (upstream's recipe has no -O)
# rebased: the series already carries 0300/0301/0302w (TERM10's a0407114b): only the deltas 0304/0305w go on top
{ damage ? true, series ? ../../../cocoa, qcOptimised ? false, rebased ? false }:
let
  C = ../../../cocoa;
  F = ../..;
  a = import (C + "/arm64.nix");
  p = import (C + "/patched.nix");
  ps = import (series + "/patched.nix");
  pkgs = a.pkgs;
  patchSrc = name: src: patches: pkgs.runCommand "fxperf2-${name}-src" { } ''
    mkdir -p tree/src/Frameworks/${name}
    cp -r ${src}/. tree/src/Frameworks/${name}/
    chmod -R u+w tree
    cd tree
    for x in ${toString (map (x: "${x}") patches)}; do ${pkgs.patch}/bin/patch -p1 --no-backup-if-mismatch < $x; done
    cp -r src/Frameworks/${name} $out
  '';
  sel = l: if damage then l else [ ];
  qcSrc = patchSrc "QuartzCore" ps.quartzcoreSrc (sel (if rebased then [ ./0306-quartzcore-video-planes-uploaded-as-red-green.patch ] else [ ./0300-quartzcore-damage-only-frames.patch ]));
  wsSrc = patchSrc "WindowServer" ps.windowserverSrc (sel (if rebased then [ ./0305w-windowserver-backing-store-compared-only-after-drawing.patch ] else [ ./0302w-windowserver-damage-only-compose-and-commit.patch ]));
  akSrc = patchSrc "AppKit" ps.appkitSrc (sel (if rebased then [ ./0304-appkit-layer-only-flush-says-backing-store-unchanged.patch ] else [ ./0301-appkit-gpu-layer-registered-with-its-damage.patch ]));
  q = a.pd.quartzcore-msm-arm64.override { src = qcSrc; };
  qp = q.overrideAttrs (old: { buildPhase = builtins.replaceStrings
    ([ "/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore" ] ++ (if qcOptimised then [ "$cc $cflags -c" ] else [ ]))
    ([ "/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore.pd" ] ++ (if qcOptimised then [ "$cc $cflags -O2 -fno-strict-aliasing -c" ] else [ ]))
    old.buildPhase; });
  ws = p.windowserver.overrideAttrs (old: { src = wsSrc; });
  ak = p.appkit.overrideAttrs (old: { src = akSrc; });
  cglBase = import (F + "/cgl-gpu.nix") { prefix = "/usr/local/lib/siliconforest/opengl/v1"; canonical = true; nv12Backing = true; };
  cgl = cglBase.overrideAttrs (old: { patches = old.patches ++ sel [ ./0303-cgl-rectangle-export-and-image-per-surface.patch ]; });
in pkgs.runCommand "fxperf2-frameworks-${if damage then "damage" else "series"}${if qcOptimised then "-qcO2" else ""}" { } ''
  mkdir -p $out/Frameworks/QuartzCore.framework/Versions/A $out/lib
  cp ${qp}/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore $out/Frameworks/QuartzCore.framework/Versions/A/QuartzCore.pd
  cp ${qp}/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore $out/lib/QuartzCore.pd
  cp -r ${ak}/System/Library/Frameworks/AppKit.framework $out/Frameworks/
  cp -r ${cgl}/Frameworks/OpenGL.framework $out/Frameworks/
  cp ${ws}/usr/lib/libWindowServer.dylib $out/lib/
  chmod -R u+w $out
''
