# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  openssl = import ./openssl.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-corecrypto-arm64";
  src = finch;
  patches = [ ./patches/0001-arm64-integrity-header.patch ];
  nativeBuildInputs = [ pkgs.python3 ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    cd userland/corecrypto
    # Finch's internal backend includes name build/src/openssl. Resolve the exact
    # backend source in the unpacked tree, preserving the upstream relative paths.
    mkdir -p ../../build/src/openssl/crypto
    cp -a ${openssl}/internal/ml_dsa ../../build/src/openssl/crypto/
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fno-stack-protector -fvisibility=hidden -D_DARWIN_C_SOURCE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wno-deprecated-declarations -I${libsystem}/usr/include -I${openssl}/include -I${openssl}/internal -Iinclude"
    mkdir objects
    for s in abi/*.c; do
      ${cc} $CF -c "$s" -o objects/$(basename "$s" .c).o
    done
    ${cc} -dynamiclib objects/*.o ${openssl}/lib/libcrypto.a \
      --ld-path=${ld} -nostdlib -isysroot ${sdk} \
      -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-exported_symbols_list,exports.txt \
      -install_name /usr/lib/system/libcorecrypto.dylib -compatibility_version 1 -current_version 1922.101.2 \
      -lSystem -o libcorecrypto.dylib
    python3 tools/seal-corecrypto.py seal libcorecrypto.dylib
    PYTHONPATH=${../../homebrew} python3 - <<'PYEOF'
from pathlib import Path
from hb_macho import resign
p=Path('libcorecrypto.dylib')
p.write_bytes(resign(p.read_bytes())[0])
PYEOF
  '';
  installPhase = ''
    mkdir -p $out/usr/lib/system $out/usr/share/licenses/finch-corecrypto
    cp libcorecrypto.dylib $out/usr/lib/system/
    cp ${finch}/LICENSE* $out/usr/share/licenses/finch-corecrypto/
    tar -xOf ${openssl.src} openssl-3.5.9/LICENSE.txt > $out/usr/share/licenses/finch-corecrypto/OpenSSL-LICENSE.txt
  '';
  dontFixup = true;
  dontStrip = true;
}
