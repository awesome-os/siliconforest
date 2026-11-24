# agx-offscreen-test for the SiliconForest M1 guest (arm64 Mach-O), with the Mesa and libdrm it runs
# on, laid out as a self-contained bundle that does not touch the image's own /usr/lib Mesa:
#
#   usr/local/bin/agx-offscreen-test
#   usr/local/bin/agx-panel-demo        (GPU scenes shown on the panel through SFGOP, timings)
#   usr/local/lib/mesa-asahi/{libEGL.1,libGLESv2.2,libgallium-26.3.0-devel,libdrm.2}.dylib
#
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes agx-offscreen-test.nix
let
  c = import ./common.nix;
  inherit (c) pkgs p triple sdkRoot;
  mesa = import ./mesa-darwin.nix;
  libdrm = p.libdrm-dri-arm64;
  libSystem = p.libsystem-arm64;
  tc = p.arm64-cross-toolchain;
  prefix = "/usr/local/lib/mesa-asahi";
  # the compositor's static SFGOP + matching static IOKit (not libIOKitCF, see that recipe)
  sfgop = import ../chonkstep-darwin/build-sfgop.nix;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-agx-offscreen-test-arm64-darwin";
  dontUnpack = true;
  buildPhase = ''
    ${tc}/bin/${triple}-clang -std=gnu11 -O2 -Wall -Wextra \
      -isysroot ${sdkRoot} -mmacosx-version-min=26.5 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
      -fno-stack-protector -I${libSystem}/usr/include \
      -I${mesa}/usr/include -I${libdrm}/include -I${libdrm}/include/libdrm \
      ${./test/agx-offscreen-test.c} \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -L${mesa}/usr/lib -L${libdrm}/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lEGL -lGLESv2 -ldrm -lSystem -o agx-offscreen-test
    # The SFGOP archives carry no symbol table ld64 accepts (rustc, their other user, takes the
    # members itself), so link the members: all of both archives, as the compositor does.
    mkdir sfgop && (cd sfgop && ${tc}/bin/${triple}-ar x ${sfgop}/lib/libSFGOP.a && ${tc}/bin/${triple}-ar x ${sfgop}/lib/libIOKit.a)
    ${tc}/bin/${triple}-clang -std=gnu11 -O2 -Wall -Wextra -DHAVE_SFGOP \
      -isysroot ${sdkRoot} -mmacosx-version-min=26.5 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
      -fno-stack-protector -I${libSystem}/usr/include -I${mesa}/usr/include -I${sfgop}/include \
      -I${libdrm}/include -I${libdrm}/include/libdrm \
      ${./test/agx-panel-demo.c} sfgop/*.o \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -L${mesa}/usr/lib -L${libdrm}/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lEGL -lGLESv2 -ldrm -lSystem -o agx-panel-demo
  '';
  installPhase = ''
    INT=${tc}/bin/${triple}-install_name_tool
    OTOOL=${tc}/bin/${triple}-otool
    lib=$out${prefix}
    mkdir -p $out/usr/local/bin $lib
    cp agx-offscreen-test agx-panel-demo $out/usr/local/bin/
    cp ${mesa}/usr/lib/libEGL.1.dylib ${mesa}/usr/lib/libGLESv2.2.dylib \
       ${mesa}/usr/lib/libgallium-*.dylib ${libdrm}/lib/libdrm.2.dylib $lib/
    chmod u+w $lib/* $out/usr/local/bin/*
    # every reference to one of the bundled libraries, whatever directory it was recorded with,
    # becomes ${prefix}/<name>
    for f in $lib/*.dylib $out/usr/local/bin/*; do
      case "$f" in *.dylib) "$INT" -id "${prefix}/$(basename "$f")" "$f";; esac
      for d in $lib/*.dylib; do
        b=$(basename "$d")
        for old in $("$OTOOL" -L "$f" | awk 'NR>1 {print $1}' | grep "/$b\$" || true); do
          "$INT" -change "$old" "${prefix}/$b" "$f"
        done
      done
    done
    { echo "# otool -L after relocation"; for f in $out/usr/local/bin/* $lib/*.dylib; do "$OTOOL" -L "$f"; done; } > $out/LINKAGE.txt
  '';
  dontFixup = true;
  dontStrip = true;
}
