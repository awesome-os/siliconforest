# drm-storm for the SiliconForest M1 guest (arm64 Mach-O).  It links the libdrm of the Mesa bundle
# (/usr/local/lib/mesa-asahi/libdrm.2.dylib, staged with the GL tests) and nothing else.
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes drm-storm.nix
let
  c = import ../m1-mesa-asahi/common.nix;
  inherit (c) pkgs p triple sdkRoot;
  libdrm = p.libdrm-dri-arm64;
  libSystem = p.libsystem-arm64;
  tc = p.arm64-cross-toolchain;
  prefix = "/usr/local/lib/mesa-asahi";
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-drm-storm-arm64-darwin";
  dontUnpack = true;
  buildPhase = ''
    ${tc}/bin/${triple}-clang -std=gnu11 -O2 -Wall -Wextra \
      -isysroot ${sdkRoot} -mmacosx-version-min=26.5 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
      -fno-stack-protector -I${libSystem}/usr/include -I${libdrm}/include -I${libdrm}/include/libdrm \
      ${./test/drm-storm.c} \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -L${libdrm}/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -ldrm -lSystem -o drm-storm
  '';
  installPhase = ''
    INT=${tc}/bin/${triple}-install_name_tool
    OTOOL=${tc}/bin/${triple}-otool
    mkdir -p $out/usr/local/bin
    cp drm-storm $out/usr/local/bin/
    chmod u+w $out/usr/local/bin/drm-storm
    for old in $("$OTOOL" -L $out/usr/local/bin/drm-storm | awk 'NR>1 {print $1}' | grep '/libdrm.2.dylib$' || true); do
      "$INT" -change "$old" "${prefix}/libdrm.2.dylib" $out/usr/local/bin/drm-storm
    done
    ${pkgs.python3}/bin/python3 ${../m1-mesa-asahi/tools/cs-rehash.py} $out/usr/local/bin/drm-storm
  '';
  dontFixup = true;
  dontStrip = true;
}
