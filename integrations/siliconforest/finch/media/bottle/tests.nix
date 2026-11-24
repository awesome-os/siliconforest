# SPDX-License-Identifier: MIT
let
  e=import ../../environment.nix;
  a=import ../../../cocoa/arm64.nix;
  inherit(e) pkgs p libsystem sdk cc ld;
  avf=import ./avfoundation.nix;
  ava=import ../../AVFAudio/engine/build.nix;
  at=import ../../AudioToolbox/codecs.nix;
  cm=import ../../CoreMedia/build.nix;
  ci=import ./coreimage.nix;
  metal=import ../../../cocoa/build-own-frameworks.nix;
in pkgs.stdenv.mkDerivation {
  name="siliconforest-ffmpeg-bottle-capability-tests-arm64";
  dontUnpack=true;dontConfigure=true;
  buildPhase=''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir -p headers/AudioToolbox
    cp ${./capture.h} capture.h
    cp ${at}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/Generated.h headers/AudioToolbox/
    ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fobjc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined \
      -I$PWD -I${../../AVFoundation/include} -I${../../AVFAudio/engine/include} -I${../../AVFAudio/include} -I${../../AudioToolbox/include} -I$PWD/headers \
      -I${import ../headers.nix} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${e.pd.outPath}/src/Frameworks \
      -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
      ${./capabilities.m} --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib \
      -F${avf}/System/Library/Frameworks -F${ava}/System/Library/Frameworks -F${at}/System/Library/Frameworks -F${cm}/System/Library/Frameworks -F${ci}/System/Library/Frameworks -F${metal}/System/Library/Frameworks \
      -framework AVFoundation -framework AVFAudio -framework AudioToolbox -framework CoreMedia -framework CoreImage -framework Metal \
      -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${../../AudioToolbox/codec/retained-provider.tbd} -Wl,-dylib_file,/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore:${./retained-quartzcore.tbd} \
      -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -o bottle-capabilities
  '';
  installPhase=''mkdir -p $out/bin;cp bottle-capabilities $out/bin/'';
  dontFixup=true;dontStrip=true;
}
