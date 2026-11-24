# SPDX-License-Identifier: MIT
{ network ? import ./sc-network.nix }:
let a=import ../cocoa/arm64.nix; inherit(a) pd pkgs;
 sdk="${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 system=pd.libsystem-arm64; sc=network;
in pkgs.runCommand "sffx-network-backend-probe-arm64" {} ''
 mkdir include
 ln -s ${a.base.corefoundation}/include include/CoreFoundation
 ln -s ${sc}/System/Library/Frameworks/SystemConfiguration.framework/Headers include/SystemConfiguration
 ${pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang -O2 -w -isysroot ${sdk} \
  -I${system}/usr/include -Iinclude -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
  -L${system}/usr/lib -L${a.base.corefoundation}/usr/lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 ${./sc-network-probe.c} \
  ${sc}/System/Library/Frameworks/SystemConfiguration.framework/Versions/A/SystemConfiguration \
  -lCoreFoundation -lSystem -o sc-network-probe
 mkdir -p $out/bin;cp sc-network-probe $out/bin/
''
