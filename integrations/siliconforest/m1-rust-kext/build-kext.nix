# Builds SFRustHello.kext (C++ IOKit shell + Rust object) in the pinned SiliconForest tree,
# the same way ../m1-apcie-host/build-kext.nix builds its kext.
# $out/nix-support/build-commands has the exact compile and link lines ninja ran.
let
  inherit (import ./pins.nix) pd pkgs;
  rustObject = (import ./build-rust.nix).object;
  copied = pkgs.runCommand "siliconforest-rust-hello-source" {} ''
    mkdir -p "$out/rust"
    cp ${./kext/CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./kext/Info.plist} "$out/Info.plist"
    cp ${./kext/SFRustHello.cpp} "$out/SFRustHello.cpp"
    cp ${./kext/sf_rust_hello.h} "$out/sf_rust_hello.h"
    cp ${rustObject}/sfrusthello.o "$out/rust/sfrusthello.o"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-rust-hello";
  src = pkgs.runCommand "siliconforest-rust-hello-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFRustHello"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFRustHello"
    printf '\nadd_subdirectory(SFRustHello)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix -t commands SFRustHello.kext > build-commands.txt
    ninja -C build-nix SFRustHello.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFRustHello/SFRustHello.kext
    test -f "$bundle/Contents/MacOS/SFRustHello"
    mkdir -p "$out/System/Library/Extensions" "$out/nix-support"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    cp build-commands.txt "$out/nix-support/build-commands"
    runHook postInstall
  '';
})
