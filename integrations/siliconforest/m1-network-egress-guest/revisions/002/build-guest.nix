let
  pd = builtins.getFlake "git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev=e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  system = import ../../../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
  input = pkgs.lib.fileset.toSource { root = ./.; fileset = pkgs.lib.fileset.unions [
    ./guest/net_egress.c ./guest/process_context.c ./guest/process_context.h
    ./codec/egress_validate.c ./codec/egress_validate.h
    ./interfaces/interface_state.c ./interfaces/interface_darwin.c
    ./interfaces/interface_internal.h ./interfaces/owned_interfaces.h
    ./route/query_routes.c ./route/query_routes.h ./route/route_wire.c
    ./route/route_wire.h ./route/route_abi.c
  ]; };
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-net-egress-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" \
      -I${../../../m1-network-transport/codec} -I${input}/codec -I${input}/interfaces \
      ${input}/guest/net_egress.c ${input}/guest/process_context.c \
      ${input}/codec/egress_validate.c ${input}/interfaces/interface_state.c \
      ${input}/interfaces/interface_darwin.c ${input}/route/query_routes.c \
      ${input}/route/route_wire.c ${input}/route/route_abi.c \
      ${../../../m1-network-transport/codec/transport.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -L${system}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains -lSystem \
      -o darwin-net-egress
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-net-egress > header.txt
    grep -Eq 'ARM64[[:space:]].*EXECUTE' header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-net-egress > dependencies.txt
    test "$(tail -n +2 dependencies.txt | wc -l)" -eq 1
    grep -q '/usr/lib/libSystem.B.dylib' dependencies.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/darwin-net-egress"
    cp darwin-net-egress "$out/usr/bin/"
    cp header.txt dependencies.txt "$out/share/darwin-net-egress/"
  '';
  dontFixup = true;
  dontStrip = true;
}
