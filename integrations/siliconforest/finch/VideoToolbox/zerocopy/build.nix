# SPDX-License-Identifier: MIT
# Private producer prototype. Old ABI/default copy path stays available; strict
# SF_VT_ZERO_COPY=require fails if the full storage/lifetime contract is absent.
let
 e=import ../../environment.nix;
 a=import ../../../cocoa/arm64.nix;
 base=import ../encoder/build.nix;
 ff=import ./ffmpeg.nix;
 oldFF=import ../../../m1-avd/build-ffmpeg-sfavd.nix;
 cv=import ./corevideo.nix;
 oldCV=import ../../CoreVideo/build.nix;
 user=import ../../../m1-avd/build-user.nix;
 fw="/System/Library/Frameworks";
 rewrite=builtins.replaceStrings ["${oldFF}" "${oldCV}"] ["${ff}" "${cv}"];
in base.overrideAttrs(old:{
 name="siliconforest-videotoolbox-external-capture-stage-arm64";
 buildPhase=builtins.replaceStrings
  ["$CC $CFLAGS -c VideoToolbox.c -o VideoToolbox.o" "exports.txt VideoToolbox.o"]
  [''
   cp ${./producer.h} SFGPUProducer.h
   cp ${./lease.h} lease.h
   python3 ${./port.py} VideoToolbox.c
   CFLAGS="$CFLAGS -I${../../CoreVideo/include} -I${user}/include -I${a.base.foundation}/usr/include -I${a.base.libobjc}/usr/include -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks"
   $CC $CFLAGS -I. -fno-objc-arc -fobjc-exceptions -c ${./lease.m} -o SFFrameLease.o
   $CC $CFLAGS -c VideoToolbox.c -o VideoToolbox.o
  '' "exports.txt SFFrameLease.o VideoToolbox.o -L${a.base.foundation}/usr/lib -L${a.base.libobjc}/usr/lib -lFoundation -lobjc -F${a.iosurface}${fw} -framework IOSurface"] (rewrite old.buildPhase);
 installPhase=rewrite old.installPhase;
})
