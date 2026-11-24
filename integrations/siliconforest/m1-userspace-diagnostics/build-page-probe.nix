let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-page-probe-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -Wno-error=deprecated-declarations \
      -isystem ${p.libsystem-arm64}/usr/include \
      ${./page-probe.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${p.libsystem-arm64}/usr/lib -L${p.libsystem-arm64}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-page-probe
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-page-probe > header.txt
    grep -Eq 'ARM64[[:space:]].*EXECUTE' header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-page-probe > dependencies.txt
    test "$(tail -n +2 dependencies.txt | wc -l)" -eq 1
    grep -q '/usr/lib/libSystem.B.dylib' dependencies.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/darwin-lab-page-probe"
    cp darwin-page-probe "$out/usr/bin/"
    cp header.txt dependencies.txt "$out/share/darwin-lab-page-probe/"
  '';
  dontFixup = true;
  dontStrip = true;
}
