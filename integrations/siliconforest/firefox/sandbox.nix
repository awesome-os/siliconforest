# SPDX-License-Identifier: MIT
# Private interposition of only sandbox_init APIs, never a libSystem replacement.
# Reuse the exact compiler and permission translation proven by SF-U in QEMU.
{ prefix ? "/private/var/db/sffx/sandbox-v4", coldAVDFirmware ? false, fontDirectory ? null }:
let
  a = import ../cocoa/arm64.nix;
  inherit (a) pkgs pd;
  system = pd.libsystem-arm64;
  tc = pd.arm64-cross-toolchain;
  triple = "arm64-apple-darwin20.4";
  sdk = "${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
in pkgs.stdenv.mkDerivation {
  pname = "siliconforest-firefox-sandbox-compiler-arm64";
  version = "1";
  src = pkgs.runCommand "siliconforest-firefox-sandbox-source" {} ''
    mkdir -p $out/src/Libraries
    cp -r ${a.src}/src/Libraries/libsandbox $out/src/Libraries/
  '';
  patches = [
    ../cocoa/patches/0007-tip-libsandbox-sbpl-functions-and-profiles.patch
    ../cocoa/patches/0008-libsandbox-profile-language-for-firefox.patch
    # 0014 also carries a separate dirhelper hunk. This DSO owns only the
    # profile compiler; retain precisely its libsandbox portion.
    (pkgs.writeText "0014-libsandbox-only.patch" (builtins.head (pkgs.lib.splitString
      "--- a/src/Libraries/libSystem/stub/sf_dirhelper.c"
      (builtins.readFile ../cocoa/patches/0014-libsandbox-window-server-permission-is-the-compositor-socket.patch))))
    ./sandbox-patches/0001-explicit-accelerator-device-permission.patch
    ./sandbox-patches/0002-explicit-font-server-read-permission.patch
    ./sandbox-patches/0003-explicit-accelerator-render-node-permission.patch
  ] ++ pkgs.lib.optional coldAVDFirmware ./sandbox-patches/0004-cold-avd-firmware-read-for-explicit-accelerator.patch;
  dontConfigure = true;
  buildPhase = ''
    CC=${tc}/bin/${triple}-clang
    CFLAGS="-O2 -g -fPIC -fvisibility=hidden -isysroot ${sdk} -I${system}/usr/include -Isrc/Libraries/libsandbox/include -Isrc/Libraries/libsandbox"
    for name in sandbox sbpl; do
      $CC $CFLAGS ${pkgs.lib.optionalString (fontDirectory != null) "-DSFFX_FONT_DIRECTORY='\"${fontDirectory}\"'"} -Dsandbox_init=sffx_sandbox_init \
        -Dsandbox_init_with_parameters=sffx_sandbox_init_with_parameters \
        -Dsandbox_free_error=sffx_sandbox_free_error \
        -c src/Libraries/libsandbox/$name.c -o $name.o
    done
    $CC $CFLAGS -c ${./sandbox-interpose.c} -o interpose.o
    $CC -dynamiclib -isysroot ${sdk} -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
      sandbox.o sbpl.o interpose.o -L${system}/usr/lib -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 \
      -Wl,-install_name,${prefix}/libsffx-sandbox.dylib \
      -o libsffx-sandbox.dylib
  '';
  installPhase = ''
    mkdir -p $out
    cp libsffx-sandbox.dylib $out/
  '';
  dontFixup = true;
  dontStrip = true;
}
