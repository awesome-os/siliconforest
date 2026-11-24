# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  accounts = import ../Accounts/build.nix;
  auth = import ../LocalAuthentication/build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-dropin-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-arc -fobjc-exceptions -include stdio.h -Wno-deprecated-declarations -Wno-availability -Wno-nullability-completeness -I${../Accounts/include} -I${../LocalAuthentication/include} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include"
    LF="--ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${accounts}/System/Library/Frameworks -F${auth}/System/Library/Frameworks -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    ${cc} $CF userland/tests/accounts-test.m $LF -framework Accounts -lFoundation -lobjc -lSystem -o bin/accounts-test
    ${cc} $CF ${./dropins.m} $LF -framework Accounts -framework LocalAuthentication -lFoundation -lobjc -lSystem -o bin/dropins
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
