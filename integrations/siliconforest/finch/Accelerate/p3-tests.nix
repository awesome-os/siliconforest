# SPDX-License-Identifier: MIT
let
 e=import ../environment.nix;
 graphics=import ../../cocoa/patched.nix;
 a=import ../../cocoa/arm64.nix;
 whole=import ./installed-graphics.nix;
 colorsync=import ../ColorSync/build.nix;
 headers=import ../media/headers.nix;
in e.pkgs.runCommand "siliconforest-vimage-p3-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -isysroot ${e.sdk} -O2 -fblocks -DSF_COMPONENT_vImage \
  -I${whole}/headers -I${headers} -I${graphics.coregraphics}/usr/include -I${graphics.onyx2d}/usr/include \
  -I${e.p.coreFoundation-arm64}/include -I${e.p.libobjc-arm64}/usr/include -I${e.libsystem}/usr/include \
  -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
  ${./p3-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib \
  -L${graphics.coregraphics}/usr/lib -L${graphics.onyx2d}/usr/lib -L${whole}/staged-bootstrap \
  -F${colorsync}/System/Library/Frameworks -framework ColorSync -lvImage -lCoreGraphics -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/p3-probe
''
