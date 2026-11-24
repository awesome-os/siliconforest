# darwin-net-probe for the PureDarwin M1 guest (arm64 Mach-O, libSystem only).
# Same toolchain, SDK and link shape as m1-desktop-session/shared-shell/build-records.nix.
# Links against the libSystem that the current RAM roots carry (spawn-attribute
# fix 9fc03c87, same exports as canonical 8ff49e0c). Build only; no staging.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-net-probe-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" -I${./probe} \
      ${./probe/darwin-net-probe.c} ${./probe/net_wire.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-net-probe
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-net-probe > header.txt
    grep -Eq 'ARM64[[:space:]].*EXECUTE' header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-net-probe > dependencies.txt
    test "$(tail -n +2 dependencies.txt | wc -l)" -eq 1
    grep -q '/usr/lib/libSystem.B.dylib' dependencies.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/darwin-net-probe"
    cp darwin-net-probe "$out/usr/bin/"
    cp ${./supervisor/darwin-net-probe-test} "$out/usr/bin/darwin-net-probe-test"
    chmod 0755 "$out/usr/bin/darwin-net-probe-test"
    cp header.txt dependencies.txt "$out/share/darwin-net-probe/"
  '';
  dontFixup = true;
  dontStrip = true;
}
