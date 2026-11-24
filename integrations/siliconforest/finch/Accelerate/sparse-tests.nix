# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  sparse = import ./build.nix { groups = [ "SparseBLAS" ]; };
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-SparseBLAS-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin
    for source in userland/tests/sparseblas-test.c ${./sparse-probe.c}; do
      if [ "$source" = userland/tests/sparseblas-test.c ]; then executable=sparseblas-test; else executable=sparse-probe; fi
      ${cc} -isysroot ${sdk} -mmacosx-version-min=26.0 -O2 -DSF_COMPONENT_SparseBLAS \
        -I${sparse}/headers -I${libsystem}/usr/include "$source" --ld-path=${ld} -nostdlib \
        -L${sparse}/staged-bootstrap -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system \
        -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
        -Wl,-platform_version,macos,26.0,26.0 -lSparseBLAS -lSystem -o bin/$executable
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
