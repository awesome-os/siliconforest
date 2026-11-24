# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  colors = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-ColorSync-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    ${cc} -isysroot ${sdk} -mmacosx-version-min=26.0 -O2 -fblocks -D_DARWIN_C_SOURCE \
      -F${colors}/System/Library/Frameworks -I${p.coreFoundation-arm64}/include -I${libsystem}/usr/include \
      userland/tests/colorsync-test.c --ld-path=${ld} -nostdlib -F${colors}/System/Library/Frameworks \
      -L${p.coreFoundation-arm64}/usr/lib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.0,26.0 -framework ColorSync -lCoreFoundation -lSystem -o colorsync-test
  '';
  installPhase = ''mkdir -p $out/bin; cp colorsync-test $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
