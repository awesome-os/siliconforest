# SPDX-License-Identifier: MIT
let
  a = import ../cocoa/arm64.nix;
  inherit (a) pkgs pd;
  tc = pd.arm64-cross-toolchain;
  triple = "arm64-apple-darwin20.4";
  sdk = "${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  system = pd.libsystem-arm64;
in pkgs.stdenv.mkDerivation {
  pname = "siliconforest-firefox-probes-arm64";
  version = "1";
  dontUnpack = true;
  buildPhase = ''
    ${tc}/bin/${triple}-clang -O2 -g -isysroot ${sdk} \
      -I${system}/usr/include -c ${./cgl-probe.c} -o cgl-probe.o
    ${tc}/bin/${triple}-clang -isysroot ${sdk} \
      -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 cgl-probe.o -o cgl-probe
    ${tc}/bin/${triple}-clang -O2 -g -isysroot ${sdk} \
      -I${system}/usr/include -I${a.src}/src/Libraries/libsandbox/include \
      -c ${./sandbox-probe.c} -o sandbox-probe.o
    ${tc}/bin/${triple}-clang -isysroot ${sdk} \
      -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 sandbox-probe.o -o sandbox-probe
    ${tc}/bin/${triple}-clang -O2 -g -isysroot ${sdk} \
      -I${system}/usr/include -I${a.src}/src/Libraries/libsandbox/include \
      -c ${./avd-sandbox-probe.c} -o avd-sandbox-probe.o
    ${tc}/bin/${triple}-clang -isysroot ${sdk} \
      -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 avd-sandbox-probe.o -o avd-sandbox-probe
    ${tc}/bin/${triple}-clang -O2 -g -isysroot ${sdk} \
      -I${system}/usr/include -I${a.src}/src/Libraries/libsandbox/include \
      -c ${./font-sandbox-probe.c} -o font-sandbox-probe.o
    ${tc}/bin/${triple}-clang -isysroot ${sdk} \
      -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 font-sandbox-probe.o -o font-sandbox-probe
    ${tc}/bin/${triple}-clang -O2 -g -isysroot ${sdk} \
      -I${system}/usr/include -c ${./io-probe.c} -o io-probe.o
    ${tc}/bin/${triple}-clang -isysroot ${sdk} \
      -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 io-probe.o -o io-probe
    ${tc}/bin/${triple}-clang -O2 -isysroot ${sdk} \
      -I${system}/usr/include -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 ${./url-argv-probe.c} -o url-argv-probe
  '';
  installPhase = ''mkdir -p $out/bin; cp cgl-probe sandbox-probe avd-sandbox-probe font-sandbox-probe io-probe url-argv-probe $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
