# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  a = import ../../cocoa/arm64.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-CoreVideo-arm64";
  src = finch;
  patches = [ ./patches/0001-sf-runtime-layout.patch ./patches/0002-optional-icc-provider.patch ];
  nativeBuildInputs = [ pkgs.python3 ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    cp -r ${./include} headers; chmod -R u+w headers
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -Wno-nullability-completeness -Wno-macro-redefined -fblocks -D_DARWIN_C_SOURCE -I$PWD/headers -I${libsystem}/usr/include -I${p.coreFoundation-arm64}/include -I${p.foundation-arm64}/usr/include -I${p.libobjc-arm64}/usr/include -I${p.coregraphics-arm64}/usr/include -I${p.onyx2d-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/include -I${a.mesa}/usr/include -F${a.iosurface}/System/Library/Frameworks -F${a.openglFramework}/System/Library/Frameworks"
    mkdir objects
    for s in userland/CoreVideo/*.[cm]; do
      FLAGS=""; case $s in *.m) FLAGS=-fobjc-arc;; esac
      ${cc} $CF $FLAGS -DSF_MEDIA_PREPROCESS -E -P "$s" -o objects/$(basename "$s").i
    done
    python3 ${../media/declarations.py} ${../tools/c_functions.py} userland/CoreVideo headers/CoreVideo/Generated.h CV objects/*.i
    for s in userland/CoreVideo/*.[cm]; do
      FLAGS=""; case $s in *.m) FLAGS=-fobjc-arc;; esac
      ${cc} $CF $FLAGS -c "$s" -o objects/$(basename "$s").o
    done
    ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${p.coregraphics-arm64}/usr/lib -L${p.onyx2d-arm64}/usr/lib -I${a.mesa}/include -I${a.mesa}/usr/include -F${a.iosurface}/System/Library/Frameworks -F${a.openglFramework}/System/Library/Frameworks -Wl,-dylib_file,/usr/lib/libGL.1.dylib:${a.mesa}/usr/lib/libGL.1.dylib -Wl,-dylib_file,/usr/lib/libGLU.1.dylib:${a.glu}/usr/lib/libGLU.1.dylib -L${a.mesa}/usr/lib -L${a.glu}/usr/lib -framework IOSurface -framework OpenGL -lCoreGraphics -lOnyx2D -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -install_name /System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo -current_version 734.4 -compatibility_version 1.2 -o CoreVideo
  '';
  installPhase = ''
    fw=$out/System/Library/Frameworks/CoreVideo.framework
    mkdir -p $fw/Versions/A/Resources/Licenses
    cp CoreVideo $fw/Versions/A/
    cp -r headers/CoreVideo $fw/Versions/A/Headers
    ln -s A $fw/Versions/Current
    ln -s Versions/Current/CoreVideo $fw/CoreVideo
    ln -s Versions/Current/Headers $fw/Headers
    cp LICENSE* $fw/Versions/A/Resources/Licenses/
  '';
  dontFixup = true;
  dontStrip = true;
}
