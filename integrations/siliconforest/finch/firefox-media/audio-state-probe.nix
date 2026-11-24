# SPDX-License-Identifier: MIT
let e=import ../environment.nix;unit=import ../AudioToolbox/units/build.nix; in
e.pkgs.runCommand "siliconforest-firefox-media-audio-state-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin $out/lib
 cp ${./audio-state.h} audio-state.h
 flags="-O2 -std=c11 -D_DARWIN_C_SOURCE -I. -isysroot ${e.sdk} -I${unit}/usr/include -I${e.pd.outPath}/src/Frameworks/CoreAudio -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include"
 links="--ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -F${unit}/System/Library/Frameworks -framework CoreAudio -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
 ${e.cc} $flags -fPIC -dynamiclib ${./audio-state-trace.c} $links \
  -install_name @rpath/SFHALPCMTrace.dylib -o $out/lib/SFHALPCMTrace.dylib
 ${e.cc} $flags ${./audio-state-probe.c} $links -framework AudioUnit \
  $out/lib/SFHALPCMTrace.dylib -Wl,-rpath,@executable_path/../lib -o $out/bin/audio-state-probe
''
