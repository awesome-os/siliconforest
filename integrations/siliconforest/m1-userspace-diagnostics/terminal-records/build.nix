let
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ../build-libsystem-pagesize.nix;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-terminal-records-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem ${libSystem}/usr/include ${./terminal-records.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-terminal-records
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-terminal-records > header.txt
    grep -Eq 'ARM64[[:space:]].*EXECUTE' header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-terminal-records > dependencies.txt
    test "$(tail -n +2 dependencies.txt | wc -l)" -eq 1
    grep -q '/usr/lib/libSystem.B.dylib' dependencies.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/darwin-terminal-records"
    cp darwin-terminal-records "$out/usr/bin/"
    cp header.txt dependencies.txt "$out/share/darwin-terminal-records/"
  '';
  dontFixup = true;
  dontStrip = true;
}
