# SPDX-License-Identifier: MIT
# New private DSO only; never a libSystem replacement.
let
 a = import ../cocoa/arm64.nix;
 inherit (a) pkgs pd;
 sys = pd.libsystem-arm64;
 sdk = "${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 tc = "${pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
in pkgs.runCommand "sffx-io-policy-interposer-arm64" {} ''
 ${tc} -O2 -g -fPIC -fvisibility=hidden -isysroot ${sdk} -I${sys}/usr/include \
  -c ${./io-policy.c} -o policy.o
 ${tc} -dynamiclib -isysroot ${sdk} -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
  -L${sys}/usr/lib -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 \
  -Wl,-install_name,/private/var/db/sffx/io-policy-v2/libsffx-io-policy.dylib \
  policy.o -o libsffx-io-policy.dylib
 mkdir -p $out;cp libsffx-io-policy.dylib $out/
''
