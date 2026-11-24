# SPDX-License-Identifier: MIT
{ name, programs, arc ? false, probe ? null, extraLinks ? "", foundation ? null }:
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  foundationLink = if foundation == null then p.foundation-arm64 else foundation;
  framework = import (../. + "/${name}/build.nix");
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-${name}-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions ${if arc then "-fobjc-arc" else "-fno-objc-arc"} -include stdio.h -Wno-deprecated-declarations -Wno-availability -Wno-nullability-completeness -Wno-arc-bridge-casts-disallowed-in-nonarc -I${../. + "/${name}/include"} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include"
    LF="--ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${foundationLink}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${framework}/System/Library/Frameworks -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    for n in ${pkgs.lib.concatStringsSep " " programs}; do
      ${cc} $CF userland/tests/$n.m $LF -framework ${name} -lFoundation -lCoreFoundation -lobjc -lSystem ${extraLinks} -o bin/$n
    done
    ${pkgs.lib.optionalString (probe != null) "${cc} $CF ${probe} $LF -framework ${name} -lFoundation -lCoreFoundation -lobjc -lSystem ${extraLinks} -o bin/probe"}
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
