# SPDX-License-Identifier: MIT
# The probes for the arm64 guest: probe/sf-compat-probe exercises the CoreServices calls of
# ./build-coreservices.nix, probe/sf-appkit-probe the pasteboard of ./build-appkit.nix (toolchain and link shape of ../m1-storage/userland/jit/build-jit-test.nix).
#   nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-probe.nix
let
  pd = import ../qemu-dev/sf-userland.nix;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ../libsystem-default/build-libsystem.nix;
  cs = import ./build-coreservices.nix;
  cf = p.coreFoundation-arm64;
  appkit = import ./build-appkit.nix;
  syslibs = import ../homebrew/build-syslibs.nix;
  A = "System/Library/Frameworks/AppKit.framework/Versions/C/AppKit";
  F = "System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices";
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-compat-probe-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" -I${cf}/include -I${cs}/usr/include \
      ${./probe/sf-compat-probe.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system -L${cf}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-dylib_file,/usr/lib/libCoreFoundation.dylib:${cf}/usr/lib/libCoreFoundation.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      ${cs}/${F} -lCoreFoundation -lSystem -o sf-compat-probe
    # sf-appkit-probe: AppKit by its macOS path; its own dependencies are not this link's business
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" -I${cf}/include \
      ${./probe/sf-appkit-probe.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system -L${cf}/usr/lib -L${p.libobjc-arm64}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      ${appkit}/System/Library/Frameworks/AppKit.framework/Versions/A/AppKit -lobjc -lCoreFoundation -lSystem -o sf-appkit-probe
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L sf-appkit-probe
    # sf-expat-probe, sf-curses-probe: the /usr/lib libraries of ../homebrew/build-syslibs.nix
    for t in expat:expat.1 curses:ncurses.5.4 strxfrm:System; do
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
        -std=c11 -Wall -Wextra -O1 -isystem "$DARWIN_SDK_ROOT/usr/include" \
        ${./probe}/sf-''${t%%:*}-probe.c \
        --ld-path=${p.native-ld}/bin/ld -nostdlib \
        -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system -L${syslibs}/usr/lib \
        -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
        -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
        -l''${t##*:} -lSystem -o sf-''${t%%:*}-probe
    done
  '';
  installPhase = ''
    mkdir -p "$out/usr/share/sf-test"
    cp sf-compat-probe sf-appkit-probe sf-expat-probe sf-curses-probe sf-strxfrm-probe "$out/usr/share/sf-test/"
    chmod 755 "$out"/usr/share/sf-test/*
  '';
  dontFixup = true;
  dontStrip = true;
}
