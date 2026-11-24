# SPDX-License-Identifier: MIT
# Rebuild VLC's original GPL-2.0-or-later macOS entry point on Linux.
# Upstream explicitly supports ALLOW_RUN_AS_ROOT; no identity API is replaced.
{ vlcBundle ? "/home/chrisk/src/darwin-lab/private/mac-apps/third-party/vlc-3.0.24-unpacked/VLC media player/VLC.app" }:
let
 e=import ../environment.nix;
 a=import ../../cocoa/arm64.nix;
 inherit(e) pkgs p cc ld sdk libsystem;
 mainSource=pkgs.fetchurl {
   url="https://raw.githubusercontent.com/videolan/vlc/3.0.24/bin/darwinvlc.m";
   hash="sha256-jD/88qaLb5cXsJuvSkmo0ZyU1s5NgD531kDIxPST/EA=";
 };
 libraries=builtins.path {path=builtins.toPath (vlcBundle+"/Contents/MacOS/lib");name="vlc-3.0.24-verified-libraries";};
in pkgs.stdenv.mkDerivation {
 name="siliconforest-vlc-3.0.24-macos-entry-arm64";
 nativeBuildInputs=[pkgs.python3];
 dontUnpack=true;dontConfigure=true;
 buildPhase=''
  export DARWIN_SDK_ROOT=${sdk}
  cp ${mainSource} darwinvlc.m
  cp ${./SFVLCMain.h} SFVLCMain.h
  cp ${./SFVLCHAL.h} SFVLCHAL.h
  cp ${./SFVLCModule.h} SFVLCModule.h
  chmod u+w darwinvlc.m
  python3 ${./entry-log.py} darwinvlc.m
  substituteInPlace darwinvlc.m --replace-fail '#include <vlc/vlc.h>' '#include "SFVLCMain.h"' --replace-fail '#include <vlc_common.h>' '/* public declarations supplied above */' --replace-fail '#include <vlc_charset.h>' '/* own CF string adapter supplied above */'
  ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -DALLOW_RUN_AS_ROOT=1 -DNDEBUG -DPACKAGE_VERSION='"3.0.24"' -DPACKAGE_NAME='"VLC media player"' -I$PWD -I${libsystem}/usr/include -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${a.appDeps.appkit}/usr/include -I${e.pd.outPath}/src/Frameworks -I${p.coregraphics-arm64}/usr/include -I${p.onyx2d-arm64}/usr/include -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks darwinvlc.m --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${a.appDeps.appkit}/usr/lib -L${libraries} -lvlc -lAppKit -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -Wl,-rpath,@executable_path/lib -Wl,-rpath,@executable_path/plugins -Wl,-rpath,@executable_path/../Frameworks -o VLC-SiliconForest
 '';
 installPhase=''
  mkdir -p $out/bin $out/share/source
  cp VLC-SiliconForest $out/bin/
  cp darwinvlc.m SFVLCMain.h SFVLCHAL.h SFVLCModule.h $out/share/source/
 '';
 dontFixup=true;dontStrip=true;
}
