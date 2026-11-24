let
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  iokit = import ../../chonkstep-darwin/build-pdgop.nix;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-keyboard-inventory-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    # Repack a private copy as Darwin ar. Rust extracts the original GNU-ar
    # members itself; native ld requires the Darwin table-of-contents format.
    mkdir iokit-objects
    (
      cd iokit-objects
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar x ${iokit}/lib/libIOKit.a
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar --format=darwin crs ../libIOKit.a *.o
    )
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem ${p.libsystem-arm64}/usr/include -I${iokit}/include \
      ${./keyboard_inventory.c} libIOKit.a \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${p.libsystem-arm64}/usr/lib -L${p.libsystem-arm64}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-keyboard-inventory
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin"
    cp darwin-keyboard-inventory "$out/usr/bin/"
  '';
  dontFixup = true;
}
