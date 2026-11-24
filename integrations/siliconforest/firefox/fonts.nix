# SPDX-License-Identifier: MIT
# Diagnostic registration experiment; daily packaging uses fontconfig.nix.
# Unmodified Noto font, SIL OFL 1.1; pinned official source and license.
{ prefix ? "/private/var/db/sffx/fonts-v1" }:
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
 d=a.appDeps;
 inherit (import ./noto-fonts.nix {pkgs=a.pkgs;}) font license;
in a.pkgs.runCommand "sffx-process-font-registration-arm64" {} ''
 ${e.cc} -O2 -fPIC -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${a.base.corefoundation}/include -I${d.coretext}/usr/include -I${d.coregraphics}/usr/include \
  -DSFFX_FONT_PREFIX='"${prefix}"' -c ${./register-fonts.c} -o register.o
 ${e.cc} -dynamiclib register.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -L${a.base.corefoundation}/usr/lib -L${d.coretext}/usr/lib \
  -Wl,-install_name,${prefix}/libsffx-fonts.dylib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -lCoreText -lCoreFoundation -lSystem -o register.dylib
 mkdir -p $out
 cp register.dylib $out/libsffx-fonts.dylib
 cp ${font} $out/NotoSansDevanagari-Regular.ttf
 cp ${license} $out/OFL.txt
''
