let
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  system = import ../../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
  owner = import ../../m1-network-default-route/build.nix;
  inputRoot = pkgs.lib.fileset.toSource { root = ../..; fileset = pkgs.lib.fileset.unions [
    ./revisions/003/desktop_network.c ./owned_child.c ./owned_child.h ./paths.c ./paths.h
    ./revisions/002/session_text.c ./revisions/002/session_text.h
    ../desktop_proof/desktop_proof.c ../desktop_proof/manager_proof.c
    ../desktop_proof/desktop_proof.h ../desktop_proof/proof_internal.h
    ../record_guard/read.c ../record_guard/json.c ../record_guard/record_guard.h
    ../../m1-network-egress-guest/codec/egress_validate.h
  ]; };
  input = "${inputRoot}/m1-network-desktop-service";
  spawn = ../../chonkstep-darwin/darwin-spawn;
  process = ../../m1-network-default-client/guest;
  endpoint = ../../m1-network-resolver-service/guest;
  closing = ../../m1-network-resolver-service/revisions/002/guest;
  validation = ../../m1-network-egress-guest/codec;
  transport = ../../m1-network-transport/codec;
in pkgs.stdenv.mkDerivation {
  name = "darwin-desktop-network-session-arm64-002";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    mkdir compiler-dependencies objects
    session_index=0
    for session_unit in \
      ${input}/session/revisions/003/desktop_network.c ${input}/session/owned_child.c ${input}/session/paths.c \
      ${input}/session/revisions/002/session_text.c \
      ${input}/desktop_proof/desktop_proof.c ${input}/desktop_proof/manager_proof.c \
      ${input}/record_guard/read.c ${input}/record_guard/json.c \
      ${process}/process_context.c ${endpoint}/endpoint_owner.c ${closing}/close_guard.c \
      ${spawn}/darwin_spawn.c ${validation}/egress_validate.c ${transport}/transport.c; do
      session_index=$((session_index+1))
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
        -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
        -isystem "$DARWIN_SDK_ROOT/usr/include" \
        -I${input}/session -I${input}/session/revisions/002 -I${input}/desktop_proof \
        -I${input}/record_guard -I${process} -I${endpoint} -I${closing} -I${spawn} \
        -I${transport} -I${validation} -I${../../m1-network-default-route} \
        -MD -MF "compiler-dependencies/$session_index.d" \
        -c "$session_unit" -o "objects/$session_index.o"
    done
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      objects/*.o ${owner}/lib/libdarwin-owned-default.a \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -L${system}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains -lSystem \
      -o darwin-desktop-network
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-desktop-network > header.txt
    grep -Eq 'ARM64[[:space:]].*EXECUTE' header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-desktop-network > dependencies.txt
    test "$(tail -n +2 dependencies.txt | wc -l)" -eq 1
    grep -q '/usr/lib/libSystem.B.dylib' dependencies.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -u darwin-desktop-network > undefined.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/session-build/source"
    cp darwin-desktop-network "$out/usr/bin/"
    cp -r ${input}/. "$out/share/session-build/source/"
    cp header.txt dependencies.txt undefined.txt "$out/share/session-build/"
    cp -r compiler-dependencies objects "$out/share/session-build/"
  '';
  dontFixup = true;
  dontStrip = true;
}
