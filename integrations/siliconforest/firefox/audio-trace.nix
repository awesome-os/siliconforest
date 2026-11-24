# SPDX-License-Identifier: MIT
let
 e = import ../finch/environment.nix;
 at = import ./audio-toolbox.nix;
in e.pkgs.runCommand "sffx-audio-format-trace-arm64" {} ''
 ${e.cc} -O2 -g -fPIC -isysroot ${e.sdk} -I${e.libsystem}/usr/include -c ${./audio-trace.c} -o trace.o
 ${e.cc} -dynamiclib -isysroot ${e.sdk} --ld-path=${e.ld} -nostdlib \
  -L${e.libsystem}/usr/lib -lSystem -F${at}/System/Library/Frameworks -framework AudioToolbox \
  -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${../finch/AudioToolbox/codec/retained-provider.tbd} \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 \
  -Wl,-install_name,/private/var/db/sffx/audio-trace-v1/libsffx-audio-trace.dylib \
  trace.o -o trace.dylib
 mkdir -p $out;cp trace.dylib $out/libsffx-audio-trace.dylib
''
