# SPDX-License-Identifier: MIT
# /usr/lib/libedit.3.dylib: NetBSD libedit (the portable autotools release nixpkgs carries),
# which is what macOS ships under this name, including its readline-compatible entry points.
# Cross-built against the lab's libSystem and the ncurses of ./build-syslibs.nix.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ./build-libsystem.nix;
  syslibs = import ./build-syslibs.nix;
  sdk = "${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
  cc = "${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
  ldflags = "--ld-path=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system "
          + "-Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib "
          + "-Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains -L${syslibs}/usr/lib";
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-homebrew-libedit-arm64";
  src = pkgs.libedit.src;
  dontFixup = true;
  dontStrip = true;
  configurePhase = ''
    export CC="${cc}"
    export CFLAGS="-O2 -isysroot ${sdk} -I${libSystem}/usr/include -I${p.ncurses-arm64}/include -I${p.ncurses-arm64}/include/ncursesw -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector"
    export LDFLAGS="${ldflags}"
    export LIBS="-lncurses.5.4 -lSystem"
    ./configure --host=aarch64-apple-darwin --build=x86_64-pc-linux-gnu --disable-shared --enable-static --disable-examples --prefix=/usr \
      ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes
  '';
  buildPhase = ''
    make -C src -j$NIX_BUILD_CORES
    ${cc} -dynamiclib ${ldflags} -install_name /usr/lib/libedit.3.dylib -compatibility_version 2.0.0 -current_version 3.0.0 \
      src/*.o -lncurses.5.4 -lSystem -o libedit.3.dylib
  '';
  installPhase = ''
    mkdir -p $out/usr/lib
    cp libedit.3.dylib $out/usr/lib/
    ln -s libedit.3.dylib $out/usr/lib/libedit.dylib
    ln -s libedit.3.dylib $out/usr/lib/libedit.2.dylib
    ln -s libedit.3.dylib $out/usr/lib/libreadline.dylib
  '';
}
