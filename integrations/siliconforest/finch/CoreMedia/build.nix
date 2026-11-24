# SPDX-License-Identifier: MIT
# Whole implementation, paired with staged Finch CoreVideo through public APIs.
let
  e = import ../environment.nix;
  coreVideo = import ../CoreVideo/build.nix;
  a = import ../../cocoa/arm64.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-CoreMedia-arm64";
  src = finch;
  patches = [ ./patches/0001-sf-runtime-layout.patch ./patches/0002-notification-forward-types.patch ];
  nativeBuildInputs = [ pkgs.python3 ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    cp -r ${./include} headers; chmod -R u+w headers
    cp -r ${../CoreVideo/include}/* headers/
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -I$PWD/headers -I${libsystem}/usr/include -I${p.coreFoundation-arm64}/include -I${p.foundation-arm64}/usr/include -I${p.libobjc-arm64}/usr/include -I${p.coregraphics-arm64}/usr/include -I${p.onyx2d-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/include -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks"
    mkdir objects
    for s in userland/CoreMedia/*.[cm]; do
      FLAGS=""; case $s in *.m) FLAGS=-fobjc-arc;; esac
      ${cc} $CF $FLAGS -DSF_MEDIA_PREPROCESS -E -P "$s" -o objects/$(basename "$s").i
    done
    python3 ${../media/declarations.py} ${../tools/c_functions.py} userland/CoreMedia headers/CoreMedia/Generated.h CM objects/*.i
    for s in userland/CoreMedia/*.[cm]; do
      FLAGS=""; case $s in *.m) FLAGS=-fobjc-arc;; esac
      ${cc} $CF $FLAGS -c "$s" -o objects/$(basename "$s").o
    done
    ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${coreVideo}/System/Library/Frameworks -framework CoreVideo -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -install_name /System/Library/Frameworks/CoreMedia.framework/Versions/A/CoreMedia -current_version 3305.24.5 -compatibility_version 1 -o CoreMedia
  '';
  installPhase = ''
    fw=$out/System/Library/Frameworks/CoreMedia.framework
    mkdir -p $fw/Versions/A/Resources/Licenses
    cp CoreMedia $fw/Versions/A/
    cp -r headers/CoreMedia $fw/Versions/A/Headers
    ln -s A $fw/Versions/Current
    ln -s Versions/Current/CoreMedia $fw/CoreMedia
    ln -s Versions/Current/Headers $fw/Headers
    cp LICENSE* $fw/Versions/A/Resources/Licenses/
  '';
  dontFixup = true;
  dontStrip = true;
}
