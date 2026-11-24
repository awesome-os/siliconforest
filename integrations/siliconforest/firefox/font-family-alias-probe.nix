# SPDX-License-Identifier: MIT
# Private diagnostic DSO + CLI catalog. No installed provider replacement.
{ coreText ? false }:
let
 a=import ../cocoa/arm64.nix;
 inherit (a) pkgs pd;
 sys=pd.libsystem-arm64;
 sdk="${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 cc="${pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
in pkgs.runCommand "sffx-font-family-alias-private-probe${if coreText then "-coretext" else ""}-arm64" {} ''
 mkdir -p cf-headers $out
 ln -s ${a.base.corefoundation}/include cf-headers/CoreFoundation
 common="-O2 -g -fblocks -isysroot ${sdk} -I${sys}/usr/include -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -Icf-headers -F${a.appDeps.coretext}/System/Library/Frameworks -F${a.appDeps.coregraphics}/System/Library/Frameworks"
 ${cc} $common -c ${if coreText then ./font-coretext-alias-probe.m else ./font-family-alias-probe.m} -o alias.o
 ${cc} $common -c ${./font-family-catalog-probe.m} -o catalog.o
 for target in alias catalog; do
   extra=""
   if test "$target" = alias; then extra="-dynamiclib -Wl,-install_name,/private/var/db/sffx/font-family-alias-probe-v1/libfont-family-alias-probe.dylib"; fi
   ${cc} -isysroot ${sdk} -fuse-ld=${pd.native-ld}/bin/ld -nostdlib $extra $target.o \
     -L${sys}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib \
     -F${a.appDeps.appkit}/System/Library/Frameworks -framework AppKit -lFoundation -lCoreFoundation -lobjc -lSystem \
     -F${a.appDeps.coretext}/System/Library/Frameworks -framework CoreText \
     -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
     -Wl,-platform_version,macos,26.5,26.5 -o $target
 done
 cp alias $out/libfont-family-alias-probe.dylib
 cp catalog $out/font-family-catalog-probe
''
