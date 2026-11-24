let
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  input = pkgs.lib.fileset.toSource { root = ./.; fileset = pkgs.lib.fileset.unions [
    ./owned_child.c ./owned_child.h ./session_text.c ./session_text.h
  ]; };
  spawn = ../../chonkstep-darwin/darwin-spawn;
in pkgs.stdenv.mkDerivation {
  name = "darwin-desktop-network-session-guards-arm64-001";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" -I${spawn} -I${input} \
      -Wframe-larger-than=2048 -c ${input}/owned_child.c -o owned_child.o
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" -I${input} \
      -Wframe-larger-than=2048 -c ${input}/session_text.c -o session_text.o
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -u owned_child.o > undefined.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv owned_child.o > header.txt
    grep -Eq 'ARM64[[:space:]].*OBJECT' header.txt
  '';
  installPhase = ''
    mkdir -p "$out/lib" "$out/share/owned-child"
    cp owned_child.o session_text.o "$out/lib/"
    cp header.txt undefined.txt "$out/share/owned-child/"
  '';
  dontFixup = true;
  dontStrip = true;
}
