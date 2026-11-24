# Reuse the exact baseline's kext configure/toolchain. Only the platform bundle
# is built and installed; source patching is a separate immutable derivation.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-m1-bringup-platform";
  src = pkgs.applyPatches {
    name = "siliconforest-m1-platform-source";
    src = old.src;
    patches = [ ./platform.patch ];
  };
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFArmPlatformExpert
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFArmPlatformExpert/SFArmPlatformExpert.kext
    test -f "$bundle/Contents/MacOS/SFArmPlatformExpert"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
