let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  iokit = import ../m1-dart-survey/build-iokit-mig-header.nix;
  receipt = builtins.fromJSON (builtins.readFile ./source-validation.json);
in pkgs.stdenv.mkDerivation {
  name = "darwin-gpu-preflight-collector-arm64";
  dontUnpack = true;
  buildPhase = ''
    echo '${receipt.files_sha256."collect.c"}  ${./collect.c}' | sha256sum -c -
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    mkdir iokit-objects
    (
      cd iokit-objects
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar x ${iokit}/lib/libIOKit.a
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar --format=darwin crs ../libIOKit.a *.o
    )
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -DIOKIT=1 -DKERNEL=0 -UKERNEL \
      -isystem ${p.libsystem-arm64}/usr/include -I${iokit}/include \
      ${./collect.c} libIOKit.a --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${p.libsystem-arm64}/usr/lib -L${p.libsystem-arm64}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-gpu-preflight
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin"
    cp darwin-gpu-preflight "$out/usr/bin/"
  '';
  dontFixup = true;
}
