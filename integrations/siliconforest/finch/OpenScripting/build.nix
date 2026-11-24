# SPDX-License-Identifier: MIT
# Staged language engine only. The whole OSA facade needs CoreServices AE calls.
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-applescript-engine-arm64";
  src = finch;
  patches = [ ./patches/0001-sf-locale-initializer.patch ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fobjc-arc -D_DARWIN_C_SOURCE -Iuserland/OpenScripting -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include"
    LF="--ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -lFoundation -lCoreFoundation -lobjc -lSystem"
    mkdir objects
    for name in ASValues ASParser ASInterpreter; do
      ${cc} $CF -c userland/OpenScripting/$name.m -o objects/$name.o
    done
    ${cc} -dynamiclib objects/*.o $LF -current_version 1810 -compatibility_version 1 \
      -install_name /private/var/db/sffinch/openscripting-engine/libFinchAppleScript.dylib -o libFinchAppleScript.dylib
    ${cc} $CF ${./probe.m} $LF -Iuserland/OpenScripting -L. -lFinchAppleScript -o probe
  '';
  installPhase = ''
    mkdir -p $out/lib $out/bin $out/headers $out/licenses
    cp libFinchAppleScript.dylib $out/lib/
    cp probe $out/bin/
    cp userland/OpenScripting/AS*.h $out/headers/
    cp LICENSE* $out/licenses/
  '';
  dontFixup = true;
  dontStrip = true;
}
