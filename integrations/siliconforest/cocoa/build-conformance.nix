# SPDX-License-Identifier: MIT
# The conformance programs (./conformance/*.m) for SiliconForest arm64, compiled and linked the
# way upstream builds an AppKit application (nix/pkgs/apps/calculator.nix) against our patched
# AppKit. The same sources build on macOS with `clang -framework AppKit`.
#   -> $out/usr/local/conformance/<name>
let
  a = import ./arm64.nix;
  p = import ./patched.nix;
  inherit (a) pkgs pd common;
  sdk = "${common.appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  d = a.appDeps // { appkit = p.appkit; windowserver = p.windowserver; onyx2d = p.onyx2d; };
in pkgs.stdenv.mkDerivation {
  pname = "siliconforest-cocoa-conformance";
  version = "0.1";
  src = ./conformance;
  dontConfigure = true;
  dontFixup = true;
  dontStrip = true;
  buildPhase = ''
    cc=${common.darwinCrossToolchain}/bin/${common.targetTriple}-clang
    for f in *.m; do
      n=$(basename $f .m)
      $cc -isysroot ${sdk} -mmacosx-version-min=26.5 -D_DARWIN_C_SOURCE -DSILICONFOREST=1 -fno-stack-protector -O1 \
        -x objective-c -fno-objc-arc -fblocks -Wall -Wno-deprecated-declarations -Wno-nullability-completeness \
        -I${common.libSystem}/usr/include -I${d.libobjc}/usr/include -I${d.corefoundation}/include \
        -I${d.foundation}/usr/include -I${d.onyx2d}/usr/include -I${d.coretext}/usr/include \
        -I${d.quartzcore}/usr/include -I${pd.applicationservices}/usr/include \
        -I${d.coregraphics}/usr/include -I${d.coreservices}/usr/include -I${d.windowserver}/usr/include \
        -I${d.mesa}/usr/include \
        -F${d.appkit}/System/Library/Frameworks -F${d.openglFramework}/System/Library/Frameworks \
        -c $f -o $n.o 2> $n.warnings || { cat $n.warnings; exit 1; }
      $cc -isysroot ${sdk} -mmacosx-version-min=26.5 -fuse-ld=${common.nativeLd}/bin/ld -nostdlib \
        -L${common.libSystem}/usr/lib -L${d.libobjc}/usr/lib -L${d.corefoundation}/usr/lib \
        -L${d.foundation}/usr/lib -L${d.appkit}/usr/lib -L${d.onyx2d}/usr/lib \
        -L${d.coretext}/usr/lib -L${d.quartzcore}/usr/lib -L${d.coregraphics}/usr/lib -L${d.windowserver}/usr/lib \
        -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${common.libSystem}/usr/lib/system/libdyld.dylib \
        -Wl,-platform_version,macos,26.5,26.5 $n.o \
        -lAppKit -lQuartzCore -lCoreText -lCoreGraphics -lOnyx2D -lWindowServer -lFoundation -lCoreFoundation -lobjc -lSystem \
        -o $n
    done
  '';
  installPhase = ''
    for f in *.m; do install -Dm755 $(basename $f .m) $out/usr/local/conformance/$(basename $f .m); done
  '';
}
