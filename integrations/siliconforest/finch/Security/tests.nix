# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  security = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-security-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -D_DARWIN_C_SOURCE -Wno-deprecated-declarations -Wno-availability -I${security}/System/Library/Frameworks/Security.framework/Headers/.. -I${p.coreFoundation-arm64}/include -I${p.foundation-arm64}/usr/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include"
    # Headers are laid in a named include directory for the conventional <Security/...> spelling.
    mkdir -p headers
    ln -s ${security}/System/Library/Frameworks/Security.framework/Headers headers/Security
    CF="$CF -I$PWD/headers"
    LF="--ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${security}/System/Library/Frameworks -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -Wl,-dylib_file,/System/Library/Frameworks/Security.framework/Versions/A/Security.pd:${security}/System/Library/Frameworks/Security.framework/Versions/A/Security.pd -Wl,-dylib_file,@loader_path/Security.pd:${security}/System/Library/Frameworks/Security.framework/Versions/A/Security.pd"
    for n in security-core-test; do
      ${cc} $CF userland/tests/$n.c $LF -framework Security -lCoreFoundation -lFoundation -lobjc -lSystem -o bin/$n
    done
    for n in security-keychain-storage-test security-keychain-encryption-test; do
      ${cc} $CF -include ${./test-extra.h} -Wno-nullability-completeness -fno-objc-arc userland/tests/$n.m $LF -framework Security -lCoreFoundation -lFoundation -lobjc -lSystem -o bin/$n
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
