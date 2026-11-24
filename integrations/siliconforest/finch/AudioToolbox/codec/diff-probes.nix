# SPDX-License-Identifier: MIT
let e=import ../../environment.nix; toolbox=import ../codecs.nix; in e.pkgs.runCommand "siliconforest-audio-pcm-difference-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin headers/AudioToolbox
 cp ${toolbox}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/Generated.h headers/AudioToolbox/
 ${e.cc} -isysroot ${e.sdk} -O2 -fblocks -I${../include} -I$PWD/headers -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include -I${e.pd.outPath}/src/Frameworks \
  ${./pcm-compare-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib \
  -F${toolbox}/System/Library/Frameworks -framework AudioToolbox -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${./retained-provider.tbd} -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/pcm-compare-probe
''
