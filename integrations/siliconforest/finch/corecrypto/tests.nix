# SPDX-License-Identifier: MIT
# Finch conformance programs: never replace a missing reference with the candidate.
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  openssl = import ./openssl.nix;
  crypto = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-crypto-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir -p bin
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -D_DARWIN_C_SOURCE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wno-deprecated-declarations -I${libsystem}/usr/include -I${openssl}/include"
    LF="--ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    ${cc} $CF userland/corecrypto/tests/openssl-backend.c ${openssl}/lib/libcrypto.a $LF -lSystem -o bin/openssl-backend
    ${cc} $CF userland/tests/keychain-crypto-test.c userland/Security/SecKeychainCrypto.c ${openssl}/lib/libcrypto.a $LF -lSystem -o bin/keychain-crypto-test
    ${cc} $CF userland/corecrypto/tests/rng-entropy.c $LF -lSystem -o bin/rng-entropy
    ${cc} $CF -Iuserland/corecrypto/abi ${./tests/known-vectors.c} $LF -L${crypto}/usr/lib/system -lcorecrypto -lSystem -o bin/known-vectors
    # Independent known vectors plus differential checks against the existing
    # CommonCrypto implementation in SiliconForest's libSystem.
    ${cc} $CF -Iuserland/corecrypto/include userland/corecrypto/tests/digest-compare.c userland/corecrypto/digest.c $LF -lSystem -o bin/digest-compare
    for n in digest-abi-compare aes-abi-compare cbc-abi-compare gcm-abi-compare rng-abi-compare; do
      # The reference implementations live inside PD's Security, not a separate dylib.
      sed 's@/usr/lib/system/libcorecrypto.dylib@/System/Library/Frameworks/Security.framework/Versions/A/Security@g' userland/corecrypto/tests/$n.c > userland/corecrypto/tests/sf-$n.c
      ${cc} $CF userland/corecrypto/tests/sf-$n.c $LF -lSystem -o bin/$n
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
