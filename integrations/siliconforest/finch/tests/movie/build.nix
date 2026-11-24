# SPDX-License-Identifier: MIT
# Our ObjC movie application, compiled on Linux from own/OSS declarations.
let
  e=import ../../environment.nix;
  a=import ../../../cocoa/arm64.nix;
  inherit(e) pkgs p libsystem sdk cc ld;
  cm=import ../../CoreMedia/build.nix;
  cv=import ../../CoreVideo/sf-displaylink.nix;
  vt=import ../../media/bottle/videotoolbox.nix;
  at=import ../../AudioToolbox/install.nix;
  av=import ../../AVFAudio/engine/build.nix;
  avf=import ../../media/bottle/avfoundation.nix;
  ff=import ../../AudioToolbox/ffmpeg.nix;
  notices=import ../../AudioToolbox/ffmpeg-licenses.nix;
  headers=import ../../media/headers.nix;
in pkgs.stdenv.mkDerivation {
  name="siliconforest-finch-movie-test-arm64";
  dontUnpack=true;dontConfigure=true;
  buildPhase=''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir -p headers/AudioToolbox
    cp ${at}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/Generated.h headers/AudioToolbox/
    ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined \
      -I${../../AVFoundation/include} -I${../../AVFAudio/engine/include} -I${../../AVFAudio/include} -I${../../AudioToolbox/include} -I$PWD/headers \
      -I${headers} -I${../../../m1-avd/videotoolbox} -I${ff}/usr/include -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include \
      -I${p.coregraphics-arm64}/usr/include -I${p.onyx2d-arm64}/usr/include -I${a.appDeps.appkit}/usr/include -I${a.appDeps.quartzcore}/usr/include -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
      ${./movie.m} --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib \
      -L${p.coregraphics-arm64}/usr/lib -L${p.onyx2d-arm64}/usr/lib -L${a.appDeps.appkit}/usr/lib -L${a.appDeps.quartzcore}/usr/lib \
      -F${cm}/System/Library/Frameworks -F${cv}/System/Library/Frameworks -F${vt}/System/Library/Frameworks -F${at}/System/Library/Frameworks -F${av}/System/Library/Frameworks -F${avf}/System/Library/Frameworks -F${a.appFrameworks}/System/Library/Frameworks \
      -framework AVFoundation -framework AVFAudio -framework AudioToolbox -framework CoreMedia -framework CoreVideo -framework VideoToolbox -framework UniformTypeIdentifiers \
      -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${../../AudioToolbox/codec/retained-provider.tbd} \
      -lAppKit -lQuartzCore -lCoreGraphics -lFoundation -lCoreFoundation -lobjc -L${ff}/usr/lib -lswscale -lavutil -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o movie-test
  '';
  installPhase=''
    mkdir -p $out/bin $out/Applications/FinchMovie.app/Contents/MacOS
    cp movie-test $out/bin/
    cp movie-test $out/Applications/FinchMovie.app/Contents/MacOS/FinchMovie
    mkdir -p $out/Applications/FinchMovie.app/Contents/Resources/Licenses/FFmpeg
    cp ${notices}/* $out/Applications/FinchMovie.app/Contents/Resources/Licenses/FFmpeg/
    cat > $out/Applications/FinchMovie.app/Contents/Info.plist <<'PLIST'
    <?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict>
    <key>CFBundleExecutable</key><string>FinchMovie</string>
    <key>CFBundleIdentifier</key><string>org.siliconforest.FinchMovie</string>
    <key>CFBundleName</key><string>Finch Movie Test</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    </dict></plist>
    PLIST
  '';
  dontFixup=true;dontStrip=true;
}
