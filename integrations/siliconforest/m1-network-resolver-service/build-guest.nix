let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  system = import ../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
  owner = import ../m1-network-default-route/build.nix;
  input = pkgs.lib.fileset.toSource { root = ./.; fileset = pkgs.lib.fileset.unions [
    ./guest/net_resolver.c ./guest/child_owner.c ./guest/child_owner.h
    ./guest/endpoint_owner.c ./guest/endpoint_owner.h ./guest/probe_record.c ./guest/probe_record.h
  ]; };
  spawn = ../chonkstep-darwin/darwin-spawn;
  process = ../m1-network-default-client/guest;
  validation = pkgs.lib.fileset.toSource {
    root = ../m1-network-egress-guest/codec;
    fileset = pkgs.lib.fileset.unions [
      ../m1-network-egress-guest/codec/egress_validate.c
      ../m1-network-egress-guest/codec/egress_validate.h
    ];
  };
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-net-resolver-arm64-001";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" \
      -I${input}/guest -I${process} -I${spawn} -I${../m1-network-transport/codec} -I${validation} -I${../m1-network-default-route} \
      ${input}/guest/net_resolver.c ${process}/process_context.c \
      ${input}/guest/child_owner.c ${input}/guest/endpoint_owner.c ${input}/guest/probe_record.c \
      ${spawn}/darwin_spawn.c \
      ${validation}/egress_validate.c ${../m1-network-transport/codec/transport.c} \
      ${owner}/lib/libdarwin-owned-default.a \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -L${system}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains -lSystem \
      -o darwin-net-resolver
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-net-resolver > header.txt
    grep -Eq 'ARM64[[:space:]].*EXECUTE' header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-net-resolver > dependencies.txt
    test "$(tail -n +2 dependencies.txt | wc -l)" -eq 1
    grep -q '/usr/lib/libSystem.B.dylib' dependencies.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/darwin-net-resolver"
    cp darwin-net-resolver "$out/usr/bin/"
    cp header.txt dependencies.txt "$out/share/darwin-net-resolver/"
  '';
  dontFixup = true;
  dontStrip = true;
}
