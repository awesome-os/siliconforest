# SPDX-License-Identifier: MIT
let
 e=import ../finch/environment.nix;
 a=import ../cocoa/arm64.nix;
 at=import ./audio-toolbox.nix;
 headers=import ../finch/media/headers.nix;
in e.pkgs.runCommand "sffx-audio-HAL-clock-trace-arm64" {} ''
 ${e.cc} -O2 -g -fPIC -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${headers} -I${a.base.corefoundation}/include -I${a.src}/src/Frameworks \
  -c ${./audio-hal-trace.c} -o trace.o
 ${e.cc} -dynamiclib -isysroot ${e.sdk} --ld-path=${e.ld} -nostdlib \
  -L${e.libsystem}/usr/lib -lSystem -F${at}/System/Library/Frameworks -framework AudioToolbox \
  -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${../finch/AudioToolbox/codec/retained-provider.tbd} \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 \
  -Wl,-install_name,/private/var/db/sffx/audio-hal-trace-v2/libsffx-audio-hal-trace.dylib \
  trace.o -o trace.dylib
 mkdir -p $out;cp trace.dylib $out/libsffx-audio-hal-trace.dylib
''
