# SPDX-License-Identifier: MIT
let e = import ../environment.nix;
in e.pkgs.stdenv.mkDerivation {
  name = "siliconforest-accelerate-runtime-reference-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${e.sdk}
    for source in ${./reference-probe.c} ${./quadrature-reference.c}; do
    if [ "$source" = ${./reference-probe.c} ]; then executable=accelerate-reference; else executable=quadrature-reference; fi
    ${e.cc} -isysroot ${e.sdk} -mmacosx-version-min=26.0 -O2 -I${e.libsystem}/usr/include \
      "$source" --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.libsystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.0,26.0 -lSystem -o "$executable"
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp accelerate-reference quadrature-reference $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
