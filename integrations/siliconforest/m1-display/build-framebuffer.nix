# Compile only the firmware-framebuffer bundle with the exact pinned SDK recipe.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-m1-display-framebuffer";
  src = pkgs.applyPatches {
    name = "siliconforest-m1-display-source";
    src = old.src;
    patches = [ ./framebuffer-30bit.patch ];
  };
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix IOGOPFramebuffer.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/IOGOPFramebuffer/IOGOPFramebuffer.kext
    test -f "$bundle/Contents/MacOS/IOGOPFramebuffer"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
