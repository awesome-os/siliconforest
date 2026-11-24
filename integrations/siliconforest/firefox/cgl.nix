# SPDX-License-Identifier: MIT
# Asahi CGL and matching desktop GL; canonical install name is explicit opt-in.
{ prefix ? "/private/var/db/sffx/gl-v1", canonical ? false }:
let
  a = import ../cocoa/arm64.nix;
  inherit (a) pkgs pd;
  gl = import ./gl-api.nix { inherit prefix; };
  mesa = import ../m1-vulkan/mesa-darwin.nix;
  system = pd.libsystem-arm64;
  tc = pd.arm64-cross-toolchain;
  triple = "arm64-apple-darwin20.4";
  sdk = "${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
in pkgs.stdenv.mkDerivation {
  pname = "siliconforest-firefox-cgl-asahi-arm64";
  version = "1";
  src = pkgs.runCommand "siliconforest-firefox-cgl-source" {} ''
    mkdir -p $out/src/Frameworks
    cp -r ${a.src}/src/Frameworks/OpenGL $out/src/Frameworks/
  '';
  patches = [ ./cgl-patches/0001-native-asahi-egl.patch ];
  dontConfigure = true;
  buildPhase = ''
    F=src/Frameworks/OpenGL
    for name in CGL GLImaging; do
      ${tc}/bin/${triple}-clang -O2 -g -isysroot ${sdk} \
        -DSF_CGL_USE_EGL=1 -I$F/include -I${mesa}/usr/include \
        -I${system}/usr/include -c $F/$name.c -o $name.o
    done
    ${tc}/bin/${triple}-clang -dynamiclib -isysroot ${sdk} \
      -fuse-ld=${pd.native-ld}/bin/ld -nostdlib CGL.o GLImaging.o \
      -L${system}/usr/lib -lSystem \
      -Wl,-reexport_library,${gl}/lib/libGL.1.dylib \
      -Wl,-reexport_library,${a.glu}/usr/lib/libGLU.1.dylib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 \
      -Wl,-install_name,${if canonical then "/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL" else "${prefix}/Frameworks/OpenGL.framework/Versions/A/OpenGL"} \
      -o OpenGL
  '';
  installPhase = ''
    F=$out/Frameworks/OpenGL.framework
    mkdir -p $F/Versions/A/Resources $out/lib
    cp OpenGL $F/Versions/A/
    cat > $F/Versions/A/Resources/Info.plist <<'EOF'
    <?xml version="1.0" encoding="UTF-8"?>
    <plist version="1.0"><dict>
    <key>CFBundleExecutable</key><string>OpenGL</string>
    <key>CFBundleIdentifier</key><string>com.apple.opengl</string>
    <key>CFBundleName</key><string>OpenGL</string>
    <key>CFBundlePackageType</key><string>FMWK</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    </dict></plist>
    EOF
    ln -s A $F/Versions/Current
    ln -s Versions/Current/OpenGL $F/OpenGL
    ln -s Versions/Current/Resources $F/Resources
    cp -L ${gl}/lib/libGL.1.dylib $out/lib/
    ln -s libGL.1.dylib $out/lib/libGL.dylib
    cp -L ${a.glu}/usr/lib/libGLU.1.dylib $out/lib/
  '';
  dontFixup = true;
  dontStrip = true;
}
