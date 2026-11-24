# Original isolated read-only collector; cached pinned SDK only. No execution.
let
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  system = import ../../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
  iokit = import ../../m1-dart-survey/build-iokit-mig-header.nix;
  runtimeDyld = /nix/store/jll17vb2bl849knd0b68n358c53wh1gl-puredarwin-libsystem-0.1/usr/lib/system/libdyld.dylib;
  input = pkgs.lib.fileset.toSource { root = ./.; fileset = pkgs.lib.fileset.unions [
    ./collector.c ./collector.h ./main_mig.c
  ]; };
in pkgs.stdenv.mkDerivation {
  name = "darwin-apcie-port-observe-arm64-001";
  dontUnpack = true;
  buildPhase = ''
    echo '9fc03c87e929310d881bd5b440bf0f3d7f0b230d79521b07cea539cf5c7e481e  ${system}/usr/lib/libSystem.B.dylib' | sha256sum -c -
    echo '428e224ec2621bf3a2a7859402603a9a0fd6abe8b3e26c38edeb869e0ce58a0d  ${runtimeDyld}' | sha256sum -c -
    echo '2315eb7aaa9395eab01777f8bf5b22b6c1ae653492300d9ffa8fd18b8f413795  ${iokit}/include/device_user.h' | sha256sum -c -
    echo 'c099fba1a5376cffd8d907e82aff4d6ee077764ac4eb144c45288da5948e6491  ${iokit}/lib/libIOKit.a' | sha256sum -c -
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    mkdir iokit-objects compiler-dependencies
    (
      cd iokit-objects
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar x ${iokit}/lib/libIOKit.a
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar --format=darwin crs ../libIOKit.a *.o
    )
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang --version > compiler.txt
    for unit in collector main_mig; do
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
        -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -DIOKIT=1 -DKERNEL=0 -UKERNEL \
        -isystem "$DARWIN_SDK_ROOT/usr/include" -I${iokit}/include -I${input} \
        -MD -MF "compiler-dependencies/$unit.d" -c ${input}/$unit.c -o "$unit.o"
    done
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      collector.o main_mig.o libIOKit.a --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${system}/usr/lib -L${system}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${runtimeDyld} \
      -Wl,-dead_strip -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o darwin-apcie-port-observe
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -hv darwin-apcie-port-observe > header.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-otool -L darwin-apcie-port-observe > dependencies.txt
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -u darwin-apcie-port-observe > imports.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin" "$out/share/apcie-collector-build/source"
    cp darwin-apcie-port-observe "$out/usr/bin/"
    cp -r ${input}/. "$out/share/apcie-collector-build/source/"
    cp compiler.txt header.txt dependencies.txt imports.txt "$out/share/apcie-collector-build/"
    cp -r compiler-dependencies "$out/share/apcie-collector-build/"
    cp libIOKit.a "$out/share/apcie-collector-build/"
  '';
  dontFixup = true;
  dontStrip = true;
}
