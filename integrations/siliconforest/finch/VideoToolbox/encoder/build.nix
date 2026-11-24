# SPDX-License-Identifier: MIT
# Staged compression/pixel sessions plus the unchanged SFAppleAVD decoder.
let
 e=import ../../environment.nix;
 inherit(e) pkgs p finch;
 base=import ../../media/bottle/videotoolbox.nix;
 h264=import ./openh264.nix;
 cxx=p.libcxxDylib-arm64;
 cxxabi=p.libcxxabiDylib-arm64;
 prep=''
  cp ${finch}/userland/VideoToolbox/{VTSession.c,VTCompression.c,VTPixelSessions.c,VTH264.cpp,VTInternal.h,VTConstants.c} .
  cp ${finch}/userland/VideoToolbox/VideoToolboxConstants_Generated.m .
  cp ${../../media/bottle/vt-supplement.c} VTSupplement.c
  cp ${./SFSPS.h} SFSPS.h
  chmod u+w VT*.c VT*.cpp VT*.h
  mkdir -p headers/VideoToolbox
  cp VideoToolbox.h headers/VideoToolbox/SFDecoder.h
  python3 ${./port.py} . ${./types.h} ${../../tools/c_functions.py} ${../../media/bottle/needed-constants.txt}
  EXTRA="-I$PWD/headers -I${h264}/include/wels"
  for source in VTSession VTCompression VTPixelSessions VTExtraConstants; do
   $CC $CFLAGS $EXTRA -c $source.c -o $source.o
  done
  $CC $CFLAGS $EXTRA -I$PWD -c ${./route.c} -o VTRoute.o
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang++ $CFLAGS $EXTRA -nostdinc++ -I${cxx}/usr/include/c++/v1 -std=c++17 -D_Bool=bool -Wno-elaborated-enum-base -c VTH264.cpp -o VTH264.o
 '';
 objects="VTSession.o VTCompression.o VTPixelSessions.o VTExtraConstants.o VTRoute.o VTH264.o";
in base.overrideAttrs(old:{
 name="siliconforest-videotoolbox-encoder-stage-arm64";
 buildPhase=builtins.replaceStrings
  ["$CC $CFLAGS -I. -c ${../../media/bottle/vt-supplement.c} -o VTSupplement.o" "$NM -g VideoToolbox.o VTSupplement.o VTMoreConstants.o" "exports.txt VideoToolbox.o VTSupplement.o VTMoreConstants.o"]
  ["${prep}\n$CC $CFLAGS -I. -c VTSupplement.c -o VTSupplement.o" "$NM -g VideoToolbox.o VTSupplement.o VTMoreConstants.o ${objects}" "exports.txt VideoToolbox.o VTSupplement.o VTMoreConstants.o ${objects} ${h264}/lib/libopenh264.a -L${cxx}/usr/lib -L${cxxabi}/usr/lib -lc++ -lc++abi"] old.buildPhase;
 installPhase=old.installPhase+''
  cp -r ${h264}/share/licenses $out/share/
  chmod u+w $out/share/licenses
  mkdir -p $out/share/licenses/Finch
  cp ${finch}/LICENSE* $out/share/licenses/Finch/
  cp headers/VideoToolbox/{VideoToolbox.h,SFDecoder.h} $out/System/Library/Frameworks/VideoToolbox.framework/Versions/A/Headers/
 '';
})
