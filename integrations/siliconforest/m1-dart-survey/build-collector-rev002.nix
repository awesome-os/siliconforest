# Read-only collector using the exact tiny IOKit MIG header/archive.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  iokit = import ./build-iokit-mig-header.nix;
  inputs = builtins.fromJSON (builtins.readFile ./collector-build-inputs-002.json);
  names = [ "dart_collector.c" "dart_collector.h" "collector_main.c" ];
  source = pkgs.runCommand "darwin-dart-survey-collector-source" {} ''
    mkdir -p "$out"
    cp ${./collector-native/dart_collector.c} "$out/dart_collector.c"
    cp ${./collector-native/dart_collector.h} "$out/dart_collector.h"
    cp ${./collector-native/collector_main.c} "$out/collector_main.c"
  '';
  checks = pkgs.lib.concatMapStringsSep "\n" (n:
    let sha = inputs.files.${n};
    in assert builtins.match "[0-9a-f]{64}" sha != null;
    "echo '${sha}  ${source}/${n}' | sha256sum -c -") names;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-dart-survey-collector-rev002-arm64";
  dontUnpack = true;
  buildPhase = checks + "\n" + ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    mkdir iokit-objects
    (
      cd iokit-objects
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar x ${iokit}/lib/libIOKit.a
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar --format=darwin crs ../libIOKit.a *.o
    )
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -DIOKIT=1 -DKERNEL=0 -UKERNEL \
      -isystem ${p.libsystem-arm64}/usr/include -I${iokit}/include -I${source} \
      ${source}/dart_collector.c ${source}/collector_main.c libIOKit.a \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${p.libsystem-arm64}/usr/lib -L${p.libsystem-arm64}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-dart-survey
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin"
    cp darwin-dart-survey "$out/usr/bin/"
  '';
  dontFixup = true;
}
