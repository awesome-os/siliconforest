# SPDX-License-Identifier: MIT
let
 e=import ../../environment.nix;
 a=import ../../../cocoa/arm64.nix;
 inherit(e) pkgs p cc ld sdk libsystem;
 vt=import ./build.nix;
 cm=import ../../CoreMedia/build.nix;
 cv=import ../../CoreVideo/build.nix;
 headers=import ../../media/headers.nix;
in pkgs.runCommand "siliconforest-vt-encode-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${sdk}
 mkdir -p $out/bin headers/VideoToolbox
 cp ${vt}/System/Library/Frameworks/VideoToolbox.framework/Versions/A/Headers/*.h headers/VideoToolbox/
 for probe in encode keys options; do
  case "$probe" in
   encode) probeSource=${./encode-probe.c};;
   keys) probeSource=${./keys-probe.c};;
   options) probeSource=${./options-probe.c};;
  esac
 ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -I$PWD/headers -I${headers} -I${libsystem}/usr/include -I${p.coreFoundation-arm64}/include -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks "$probeSource" --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -F${vt}/System/Library/Frameworks -F${cm}/System/Library/Frameworks -F${cv}/System/Library/Frameworks -framework VideoToolbox -framework CoreMedia -framework CoreVideo -lCoreFoundation -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/vt-$probe-probe
 done
 ''
