let
  pd = builtins.getFlake "git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev=e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  input = pkgs.lib.fileset.toSource { root = ./.; fileset = pkgs.lib.fileset.unions [
    ./default_route.c ./default_route.h ./native.c ./native.h
    ./route_wire.c ./route_wire.h ./route_abi.c ./inventory.c ./inventory.h
    ./owned/interface_state.c ./owned/interface_darwin.c
    ./owned/interface_internal.h ./owned/owned_interfaces.h
    ./route/query_routes.c ./route/query_routes.h ./route/route_wire.c
    ./route/route_wire.h ./route/route_abi.c
  ]; };
in pkgs.stdenv.mkDerivation {
  name = "darwin-owned-default-route-objects-001";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    for path in default_route.c native.c route_wire.c route_abi.c inventory.c owned/interface_state.c owned/interface_darwin.c route/query_routes.c route/route_wire.c route/route_abi.c; do
      obj=$(echo "$path" | tr / _).o
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
        -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -g \
        -isystem "$DARWIN_SDK_ROOT/usr/include" -MD -MF "$obj.d" \
        -c ${input}/"$path" -o "$obj"
    done
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar rcs libdarwin-owned-default.a *.c.o
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -u libdarwin-owned-default.a > undefined.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm --defined-only libdarwin-owned-default.a > defined.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv native.c.o > header.txt
  '';
  installPhase = ''
    mkdir -p "$out/lib" "$out/share/darwin-owned-default"
    cp libdarwin-owned-default.a "$out/lib/"
    cp *.c.o *.d undefined.txt defined.txt header.txt "$out/share/darwin-owned-default/"
  '';
  dontFixup = true;
  dontStrip = true;
}
