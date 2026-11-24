let
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ../../m1-userspace-diagnostics/build-libsystem-pagesize.nix;
  shim = ../../chonkstep-darwin/darwin-spawn;
  fixture = shim + "/runtime-probe";
  shimRecord = builtins.fromJSON (builtins.readFile (shim + "/source-validation.json"));
  fixtureRecord = builtins.fromJSON (builtins.readFile (fixture + "/source-validation.json"));
  inputs = [
    { path = shim + "/darwin_spawn.c"; dest = "shim/darwin_spawn.c"; sha256 = shimRecord.files_sha256."darwin_spawn.c"; }
    { path = shim + "/darwin_spawn.h"; dest = "shim/darwin_spawn.h"; sha256 = shimRecord.files_sha256."darwin_spawn.h"; }
    { path = fixture + "/parent.c"; dest = "shim/runtime-probe/parent.c"; sha256 = fixtureRecord.files_sha256."parent.c"; }
    { path = fixture + "/child.c"; dest = "shim/runtime-probe/child.c"; sha256 = fixtureRecord.files_sha256."child.c"; }
    { path = fixture + "/probe_common.h"; dest = "shim/runtime-probe/probe_common.h"; sha256 = fixtureRecord.files_sha256."probe_common.h"; }
  ];
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-managed-spawn-whitelist-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    mkdir -p shim/runtime-probe
    ${pkgs.lib.concatMapStringsSep "\n" (entry: ''
      cp ${entry.path} ${entry.dest}
      printf '%s  %s\n' '${entry.sha256}' '${entry.dest}' | sha256sum -c -
    '') inputs}
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" \
      shim/darwin_spawn.c shim/runtime-probe/parent.c \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-spawn-whitelist-parent
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" shim/runtime-probe/child.c \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-spawn-whitelist-child
    for binary in darwin-spawn-whitelist-parent darwin-spawn-whitelist-child; do
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv "$binary" > "$binary.header.txt"
      grep -Eq 'ARM64[[:space:]].*EXECUTE' "$binary.header.txt"
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L "$binary" > "$binary.dependencies.txt"
      test "$(tail -n +2 "$binary.dependencies.txt" | wc -l)" -eq 1
      grep -q '/usr/lib/libSystem.B.dylib' "$binary.dependencies.txt"
    done
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/darwin-spawn-whitelist"
    cp darwin-spawn-whitelist-parent darwin-spawn-whitelist-child "$out/usr/bin/"
    cp *.header.txt *.dependencies.txt "$out/share/darwin-spawn-whitelist/"
  '';
  dontFixup = true;
  dontStrip = true;
}
