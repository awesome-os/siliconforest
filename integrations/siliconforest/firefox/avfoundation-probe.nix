# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
 av=import ./avfoundation.nix;
 cm=import ../finch/CoreMedia/build.nix;
 cv=import ../finch/CoreVideo/build.nix;
 headers=import ../finch/media/headers.nix;
in e.pkgs.runCommand "sffx-current-avfoundation-probe-arm64" {} ''
 ${e.cc} -O2 -g -fblocks -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include \
  -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${headers} \
  -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
  -c ${./avfoundation-probe.m} -o probe.o
 ${e.cc} probe.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib \
  -L${a.appDeps.quartzcore}/usr/lib -L${a.appDeps.coregraphics}/usr/lib -L${a.openglFramework}/usr/lib \
  -F${av}/Frameworks -F${cm}/System/Library/Frameworks -F${cv}/System/Library/Frameworks \
  -F${(import ../finch/AVFAudio/build.nix)}/System/Library/Frameworks \
  -framework AVFoundation -framework CoreMedia -framework CoreVideo -lQuartzCore -lCoreGraphics -lOpenGL \
  -lFoundation -lCoreFoundation -lobjc -lSystem \
  -Wl,-dylib_file,/System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.pd:${av}/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.pd \
  -Wl,-dylib_file,/usr/lib/libGL.1.dylib:${a.mesa}/usr/lib/libGL.1.dylib \
  -Wl,-dylib_file,/usr/lib/libGLU.1.dylib:${a.glu}/usr/lib/libGLU.1.dylib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o probe
 ${a.pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-install_name_tool \
  -change /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.sfcompat \
  /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation probe
 ${e.pkgs.python3}/bin/python3 ${../m1-mesa-asahi/tools/cs-rehash.py} probe
 mkdir -p $out/bin;cp probe $out/bin/avfoundation-probe
''
