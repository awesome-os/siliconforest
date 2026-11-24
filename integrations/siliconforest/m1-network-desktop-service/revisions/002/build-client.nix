let
  pd = builtins.getFlake "git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev=e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  system = import ../../../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
  input = pkgs.lib.fileset.toSource { root = ./.; fileset = pkgs.lib.fileset.unions [
    ./client/net_client.c ./client/net_result.c ./client/net_client.h ./startup.env
  ]; };
  validation = ../../../m1-network-egress-guest/codec;
in pkgs.stdenv.mkDerivation {
  name = "darwin-desktop-network-client-arm64-001";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    mkdir compiler-dependencies
    for client_unit in net_client net_result; do
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
        -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
        -isystem "$DARWIN_SDK_ROOT/usr/include" -I${input}/client -I${validation} \
        -MD -MF "compiler-dependencies/$client_unit.d" \
        -c ${input}/client/$client_unit.c -o "$client_unit.o"
    done
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" -I${validation} \
      -MD -MF compiler-dependencies/egress_validate.d \
      -c ${validation}/egress_validate.c -o egress_validate.o
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      net_client.o net_result.o egress_validate.o \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -L${system}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains -lSystem \
      -o darwin-desktop-net-client
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-desktop-net-client > header.txt
    grep -Eq 'ARM64[[:space:]].*EXECUTE' header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-desktop-net-client > dependencies.txt
    test "$(tail -n +2 dependencies.txt | wc -l)" -eq 1
    grep -q '/usr/lib/libSystem.B.dylib' dependencies.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/usr/share/darwin-desktop-network" "$out/share/client-build/source"
    cp darwin-desktop-net-client "$out/usr/bin/"
    cp ${input}/startup.env "$out/usr/share/darwin-desktop-network/startup.env"
    chmod 0644 "$out/usr/share/darwin-desktop-network/startup.env"
    cp -r ${input}/. "$out/share/client-build/source/"
    cp header.txt dependencies.txt "$out/share/client-build/"
    cp -r compiler-dependencies "$out/share/client-build/"
  '';
  dontFixup = true;
  dontStrip = true;
}
