# SPDX-License-Identifier: MIT
# Device-independent file/conversion/value slice, no engine/output/player/session code.
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  coreMedia = import ../CoreMedia/build.nix;
  toolbox = import ../AudioToolbox/build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-AVFAudio-file-converter-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -Wno-incomplete-implementation -I${./include} -I${../AudioToolbox/include} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${p.coreServices-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks"
    mkdir -p objects headers/AudioToolbox
    cp ${toolbox}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/Generated.h headers/AudioToolbox/
    for name in AVAudioTime AVAudioFormat AVAudioBuffer AVAudioConverter AVAudioFile AVFConstants; do
      ${cc} $CF -I$PWD/headers -c userland/AVFAudio/$name.m -o objects/$name.o
    done
    ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${toolbox}/System/Library/Frameworks -framework AudioToolbox -F${coreMedia}/System/Library/Frameworks -framework CoreMedia -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -install_name /System/Library/Frameworks/AVFAudio.framework/Versions/A/AVFAudio -current_version 1 -compatibility_version 1 -o AVFAudio
  '';
  installPhase = ''
    fw=$out/System/Library/Frameworks/AVFAudio.framework
    mkdir -p $fw/Versions/A/Resources/Licenses
    cp AVFAudio $fw/Versions/A/
    cp -r ${./include}/AVFAudio $fw/Versions/A/Headers
    ln -s A $fw/Versions/Current
    ln -s Versions/Current/AVFAudio $fw/AVFAudio
    ln -s Versions/Current/Headers $fw/Headers
    cp LICENSE* $fw/Versions/A/Resources/Licenses/
  '';
  dontFixup = true;
  dontStrip = true;
}
