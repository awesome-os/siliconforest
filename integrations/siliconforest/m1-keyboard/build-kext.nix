# Isolated keyboard adapter build; never edits the pinned source checkout.
# Source/host tests are separate from this SDK build and from hardware evidence.
let
  pd = builtins.getFlake "git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev=e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  lab = pkgs.lib.cleanSourceWith {
    src = ./.;
    filter = path: type: !builtins.elem (baseNameOf path) [ "inventory" "__pycache__" ];
  };
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "puredarwin-m1-spi-keyboard";
  src = pkgs.runCommand "puredarwin-m1-spi-keyboard-source" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/PDM1SPIHID"
    cp -R ${lab} "$out/src/Kernel/Extensions/PDM1SPIHID"
    printf '\nadd_subdirectory(PDM1SPIHID)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix PDM1SPIHID.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/PDM1SPIHID/PDM1SPIHID.kext
    test -f "$bundle/Contents/MacOS/PDM1SPIHID"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
