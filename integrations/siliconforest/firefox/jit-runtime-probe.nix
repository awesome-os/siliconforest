# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 sys=a.pd.libsystem-arm64;
 sdk="${a.pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 cc="${a.pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
in a.pkgs.runCommand "sffx-jit-runtime-probe-arm64" {} ''
 ${cc} -O2 -g -isysroot ${sdk} -I${sys}/usr/include -c ${./jit-runtime-probe.c} -o probe.o
 ${cc} -isysroot ${sdk} -fuse-ld=${a.pd.native-ld}/bin/ld -nostdlib probe.o \
  -L${sys}/usr/lib -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o probe
 mkdir -p sign/integrations/siliconforest/m1-storage/userland/jit sign/integrations/siliconforest/homebrew
 cp ${../m1-storage/userland/jit/sf_cs_sign.py} sign/integrations/siliconforest/m1-storage/userland/jit/sf_cs_sign.py
 cp ${../homebrew/hb_macho.py} sign/integrations/siliconforest/homebrew/hb_macho.py
 ${a.pkgs.python3}/bin/python3 sign/integrations/siliconforest/m1-storage/userland/jit/sf_cs_sign.py probe signed \
  --runtime --entitlement com.apple.security.cs.allow-jit --entitlement com.apple.security.cs.allow-unsigned-executable-memory
 mkdir -p $out/bin;cp signed $out/bin/jit-runtime-probe
 chmod 755 $out/bin/jit-runtime-probe
''
