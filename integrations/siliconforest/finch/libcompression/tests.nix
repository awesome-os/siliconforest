# SPDX-License-Identifier: MIT
# Linux-built public C comparison test, also suitable for a controller-Mac run.
let
  e = import ../environment.nix;
  inherit (e) pkgs finch libsystem sdk cc ld;
  compression = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-compression-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    ${cc} -isysroot ${sdk} -mmacosx-version-min=26.0 -O2 -I${libsystem}/usr/include -I${compression}/usr/include \
      userland/tests/compression-test.c --ld-path=${ld} -nostdlib \
      -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${compression}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.0,26.0 -lcompression -lSystem -o compression-test
  '';
  installPhase = ''mkdir -p $out/bin; cp compression-test $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
