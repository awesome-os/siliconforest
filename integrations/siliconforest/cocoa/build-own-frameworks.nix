# SPDX-License-Identifier: MIT
# Frameworks that upstream PureDarwin does not have for arm64 and that are ours (./frameworks):
#   Metal       a Metal.framework for a system without a Metal device (see frameworks/Metal/Metal.m)
#   MetalKit    links Metal and has nothing of its own: Firefox's libmozinference names it as a
#               dependency and imports no symbol from it
#   Accelerate  the single-precision vDSP routines libmozinference imports
# Built like upstream's small frameworks (nix/pkgs/apple/app-frameworks.nix): one dylib each
# at its macOS install name.
let
  a = import ./arm64.nix;
  inherit (a) pkgs pd common;
  sdk = "${common.appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  fw = "/System/Library/Frameworks";
in pkgs.stdenv.mkDerivation {
  pname = "siliconforest-cocoa-own-frameworks";
  version = "0.1";
  src = ./frameworks;
  dontConfigure = true;
  dontFixup = true;
  dontStrip = true;
  buildPhase = ''
    CC=${common.darwinCrossToolchain}/bin/${common.targetTriple}-clang
    CFLAGS="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -Wall -Wno-deprecated-declarations -fno-objc-arc -fobjc-exceptions -fblocks \
      -fvisibility=hidden -D_DARWIN_C_SOURCE -fno-stack-protector -I${common.libSystem}/usr/include -I${a.base.libobjc}/usr/include \
      -I${a.base.corefoundation}/include -I${a.base.foundation}/usr/include"
    LD="-fuse-ld=${common.nativeLd}/bin/ld -nostdlib -isysroot ${sdk} -mmacosx-version-min=26.5 -dynamiclib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${common.libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -L${common.libSystem}/usr/lib -L${a.base.libobjc}/usr/lib \
      -L${a.base.corefoundation}/usr/lib -L${a.base.foundation}/usr/lib"
    $CC $CFLAGS -x objective-c -c Metal/Metal.m -o Metal.o
    $CC $LD -install_name ${fw}/Metal.framework/Versions/A/Metal -compatibility_version 1 -current_version 1 \
      Metal.o -lFoundation -lCoreFoundation -lobjc -lSystem -o Metal.dylib
    echo "// MetalKit: a dependency by name only" > empty.c
    $CC $CFLAGS -c empty.c -o empty.o
    $CC $LD -install_name ${fw}/MetalKit.framework/Versions/A/MetalKit -compatibility_version 1 -current_version 1 \
      empty.o -lSystem -o MetalKit.dylib
    $CC $CFLAGS -c Accelerate/vDSP.c -o vDSP.o
    $CC $LD -install_name ${fw}/Accelerate.framework/Versions/A/Accelerate -compatibility_version 1 -current_version 4 \
      vDSP.o -lSystem -o Accelerate.dylib
  '';
  installPhase = ''
    for f in Metal MetalKit Accelerate; do
      d=$out${fw}/$f.framework
      install -Dm755 $f.dylib $d/Versions/A/$f
      ln -s A $d/Versions/Current
      ln -s Versions/Current/$f $d/$f
    done
  '';
}
