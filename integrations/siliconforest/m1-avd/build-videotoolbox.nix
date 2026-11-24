# SPDX-License-Identifier: MIT
# VideoToolbox.framework with a working decode side (./videotoolbox/VideoToolbox.c: sessions
# on the Apple Video Decoder through FFmpeg's sfavd hwaccel, software otherwise) in place of
# the tree's stub, and vt-decode-test. Built against the arm64 CoreFoundation, CoreMedia and
# CoreVideo the Cocoa stream builds from the pinned tree (../cocoa/arm64.nix, imported, not
# changed). FFmpeg (LGPL) is linked statically and stays private to the dylib (only the VT*
# and kVT* symbols are exported); relinking with another FFmpeg is rebuilding this recipe.
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link build-videotoolbox.nix
# Output: System/Library/Frameworks/VideoToolbox.framework/Versions/A/{VideoToolbox,Headers/VideoToolbox.h}
#         usr/bin/vt-decode-test
#         test-root/System/Library/Frameworks/{VideoToolbox,CoreMedia,CoreVideo,IOSurface}: what a test boot stages
let
  a = import ../cocoa/arm64.nix;
  inherit (a) pkgs common;
  ffmpeg = import ./build-ffmpeg-sfavd.nix;
  user = import ./build-user.nix;
  sdk = "${common.appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  fw = "/System/Library/Frameworks";
  libSystem = import ../libsystem-default/build-libsystem.nix;
in pkgs.stdenv.mkDerivation {
  pname = "siliconforest-videotoolbox-avd";
  version = "0.1";
  src = ./videotoolbox;
  dontConfigure = true;
  dontFixup = true;
  dontStrip = true;
  buildPhase = ''
    CC=${common.darwinCrossToolchain}/bin/${common.targetTriple}-clang
    NM=${common.darwinCrossToolchain}/bin/${common.targetTriple}-nm
    CFLAGS="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -Wall -Wno-deprecated-declarations -fno-stack-protector \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -D_DARWIN_C_SOURCE -I${libSystem}/usr/include \
      -I${a.base.corefoundation}/include -F${a.base.corefoundation}${fw} -F${a.corevideo}${fw} -F${a.appFrameworks}${fw} \
      -F${a.appDeps.coregraphics}${fw} -I${ffmpeg}/usr/include -I."
    LD="-fuse-ld=${common.nativeLd}/bin/ld -nostdlib -isysroot ${sdk} -mmacosx-version-min=26.5 \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -L${libSystem}/usr/lib -L${a.base.corefoundation}/usr/lib \
      -F${a.corevideo}${fw} -F${a.appFrameworks}${fw}"
    $CC $CFLAGS -c VideoToolbox.c -o VideoToolbox.o
    $NM -g VideoToolbox.o | awk '$2 ~ /^[TDS]$/ && $3 ~ /^_(VT|kVT)/ { print $3 }' | sort -u > exports.txt
    test -s exports.txt
    $CC $LD -dynamiclib -install_name ${fw}/VideoToolbox.framework/Versions/A/VideoToolbox -compatibility_version 1 \
      -current_version 1 -Wl,-exported_symbols_list,exports.txt VideoToolbox.o \
      ${ffmpeg}/usr/lib/libavcodec.a ${ffmpeg}/usr/lib/libswresample.a ${ffmpeg}/usr/lib/libavutil.a ${user}/lib/libsfavd.a \
      -framework CoreMedia -framework CoreVideo -lCoreFoundation -lSystem -o VideoToolbox
    # For a test boot without the Cocoa root: CoreVideo's pixel buffers alone (the tree's
    # CVPixelBuffer.c, unchanged), without CVDisplayLink, which needs OpenGL, Mesa and LLVM.
    # Same install name; staged only by tools/avd_vt_boot.sh together with the stock CoreMedia
    # and IOSurface.
    $CC $CFLAGS -I${a.src}/src/Frameworks -c ${a.src}/src/Frameworks/CoreVideo/CVPixelBuffer.c -o CVPixelBuffer.o
    $CC $LD -F${a.iosurface}${fw} -dynamiclib -install_name ${fw}/CoreVideo.framework/Versions/A/CoreVideo \
      -compatibility_version 1 -current_version 1 CVPixelBuffer.o -framework IOSurface -lCoreFoundation -lSystem -o CoreVideo-slim
    mkdir -p fw/VideoToolbox.framework
    cp VideoToolbox fw/VideoToolbox.framework/VideoToolbox
    $CC $CFLAGS -I${./user} vt-decode-test.c -c -o vt-decode-test.o
    $CC -Ffw $LD vt-decode-test.o ${ffmpeg}/usr/lib/libavformat.a ${ffmpeg}/usr/lib/libavcodec.a ${ffmpeg}/usr/lib/libswresample.a ${ffmpeg}/usr/lib/libavutil.a \
      ${user}/lib/libsfavd.a -Ffw -framework VideoToolbox -framework CoreMedia -framework CoreVideo -lCoreFoundation -lSystem \
      -o vt-decode-test
  '';
  installPhase = ''
    d=$out${fw}/VideoToolbox.framework
    install -Dm755 VideoToolbox $d/Versions/A/VideoToolbox
    install -Dm644 VideoToolbox.h $d/Versions/A/Headers/VideoToolbox.h
    ln -s A $d/Versions/Current
    ln -s Versions/Current/VideoToolbox $d/VideoToolbox
    ln -s Versions/Current/Headers $d/Headers
    install -Dm755 vt-decode-test $out/usr/bin/vt-decode-test
    install -Dm644 exports.txt $out/share/exports.txt
    t=$out/test-root${fw}
    install -Dm755 CoreVideo-slim $t/CoreVideo.framework/Versions/A/CoreVideo
    install -Dm755 ${a.appFrameworks}${fw}/CoreMedia.framework/Versions/A/CoreMedia $t/CoreMedia.framework/Versions/A/CoreMedia
    install -Dm755 ${a.iosurface}${fw}/IOSurface.framework/Versions/A/IOSurface $t/IOSurface.framework/Versions/A/IOSurface
    install -Dm755 VideoToolbox $t/VideoToolbox.framework/Versions/A/VideoToolbox
  '';
}
