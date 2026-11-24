# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  a = import ../../cocoa/arm64.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  coreMedia = import ../CoreMedia/build.nix;
  coreVideo = import ../CoreVideo/build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-media-tests-arm64";
  src = finch;
  patches = [ ./patches/0001-cpu-only-corevideo-test.patch ./patches/0002-complete-coremedia-descriptions.patch ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin headers
    cp -r ${../CoreMedia/include}/* headers/
    chmod -R u+w headers
    cp -r ${../CoreVideo/include}/* headers/
    chmod -R u+w headers
    cp ${coreMedia}/System/Library/Frameworks/CoreMedia.framework/Versions/A/Headers/Generated.h headers/CoreMedia/
    cp ${coreVideo}/System/Library/Frameworks/CoreVideo.framework/Versions/A/Headers/Generated.h headers/CoreVideo/
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -I$PWD/headers -I${libsystem}/usr/include -I${p.coreFoundation-arm64}/include -I${p.foundation-arm64}/usr/include -I${p.libobjc-arm64}/usr/include -I${p.coregraphics-arm64}/usr/include -I${p.onyx2d-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks"
    LF="--ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${p.coregraphics-arm64}/usr/lib -F${coreMedia}/System/Library/Frameworks -F${coreVideo}/System/Library/Frameworks -F${a.iosurface}/System/Library/Frameworks -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -framework CoreMedia -framework CoreVideo -framework IOSurface -lCoreGraphics -lFoundation -lCoreFoundation -lobjc -lSystem"
    for n in coremedia-test corevideo-test; do
      ${cc} $CF userland/tests/$n.c $LF -o bin/$n
    done
    ${cc} $CF ${./probe.c} $LF -o bin/media-probe
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
