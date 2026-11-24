let
 pd = (import ../rebase/pd.nix).pd;
 pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
 p = pd.packages.x86_64-linux;
 system = import ../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
 input = pkgs.lib.fileset.toSource { root=./.; fileset=pkgs.lib.fileset.unions [ ./resolver_probe.c ./resolver_result.c ./resolver_result.h ]; };
in pkgs.stdenv.mkDerivation {
 name="darwin-native-resolver-probe-arm64-001";dontUnpack=true;
 buildPhase=''
  export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  for unit in resolver_probe resolver_result; do
   ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
    -isystem "$DARWIN_SDK_ROOT/usr/include" -MD -MF "$unit.d" -c ${input}/"$unit.c" -o "$unit.o"
  done
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang resolver_probe.o resolver_result.o \
   --ld-path=${p.native-ld}/bin/ld -nostdlib -L${system}/usr/lib -L${system}/usr/lib/system \
   -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
   -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains -lSystem -o darwin-resolver-probe
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-resolver-probe > header.txt
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-resolver-probe > dependencies.txt
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -u darwin-resolver-probe > imports.txt
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang --version > compiler.txt
 '';
 installPhase=''
  mkdir -p "$out/usr/bin" "$out/share/native-resolver-probe/source"
  cp darwin-resolver-probe "$out/usr/bin/"
  cp header.txt dependencies.txt imports.txt compiler.txt resolver_probe.d resolver_result.d "$out/share/native-resolver-probe/"
  cp ${input}/* "$out/share/native-resolver-probe/source/"
 '';
 dontFixup=true;dontStrip=true;
}
