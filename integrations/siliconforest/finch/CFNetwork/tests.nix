# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  network = import ./build.nix;
  fw = "${network}/System/Library/Frameworks/CFNetwork.framework";
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-CFNetwork-tests-arm64";
  src = finch;
  patches = [ ./patches/0003-test-require-stream-events.patch ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    # Our source-derived header also declares this private export. Keep the
    # original test's dlsym probe, giving its local pointer a separate name.
    substituteInPlace userland/tests/cfnetwork-test.c \
      --replace-fail CFHTTPMessageSetResponseURL test_set_response_url
    substituteInPlace userland/tests/cfnetwork-test.c \
      --replace-fail '"_test_set_response_url"' '"_CFHTTPMessageSetResponseURL"'
    for source in userland/tests/cfnetwork-test.c ${./probe.c}; do
      if [ "$source" = userland/tests/cfnetwork-test.c ]; then executable=cfnetwork-test; else executable=cfnetwork-probe; fi
    ${cc} -isysroot ${sdk} -mmacosx-version-min=26.0 -O2 -fblocks -D_DARWIN_C_SOURCE -include stdlib.h \
      -F${network}/System/Library/Frameworks -I${p.coreFoundation-arm64}/include -I${libsystem}/usr/include \
      "$source" --ld-path=${ld} -nostdlib -F${network}/System/Library/Frameworks \
      -L${p.foundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.0,26.0 -framework CFNetwork -lFoundation -lobjc -lCoreFoundation -lSystem -o "$executable"
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp cfnetwork-test cfnetwork-probe $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
