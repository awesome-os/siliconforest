# SPDX-License-Identifier: MIT
# Deliberately no queue/output-unit implementation or device access in this slice.
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-AudioToolbox-file-converter-arm64";
  src = finch;
  nativeBuildInputs = [ pkgs.python3 ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    cp -r ${./include} headers; chmod -R u+w headers
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -Wno-nullability-completeness -Wno-macro-redefined -fblocks -D_DARWIN_C_SOURCE -I$PWD/headers -I${libsystem}/usr/include -I${p.coreFoundation-arm64}/include -I${p.coreServices-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks"
    mkdir objects
    for name in AudioFile AudioFormat AudioFormatTables AudioConverter ExtendedAudioFile; do
      ${cc} $CF -DSF_MEDIA_PREPROCESS -E -P userland/AudioToolbox/$name.c -o objects/$name.i
    done
    python3 ${../media/declarations.py} ${../tools/c_functions.py} userland/AudioToolbox headers/AudioToolbox/Generated.h Audio,ExtAudio objects/*.i
    for name in AudioFile AudioFormat AudioFormatTables AudioConverter ExtendedAudioFile; do
      ${cc} $CF -c userland/AudioToolbox/$name.c -o objects/$name.o
    done
    ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -F${p.coreServices-arm64}/System/Library/Frameworks -framework CoreServices -lCoreFoundation -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -install_name /System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox -current_version 1000 -compatibility_version 1 -o AudioToolbox
  '';
  installPhase = ''
    fw=$out/System/Library/Frameworks/AudioToolbox.framework
    mkdir -p $fw/Versions/A/Resources/Licenses
    cp AudioToolbox $fw/Versions/A/
    cp -r headers/AudioToolbox $fw/Versions/A/Headers
    ln -s A $fw/Versions/Current
    ln -s Versions/Current/AudioToolbox $fw/AudioToolbox
    ln -s Versions/Current/Headers $fw/Headers
    cp LICENSE* $fw/Versions/A/Resources/Licenses/
  '';
  dontFixup = true;
  dontStrip = true;
}
