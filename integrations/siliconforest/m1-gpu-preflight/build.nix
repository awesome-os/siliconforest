# Source preparation only. Root evaluates/builds separately; no KC or boot.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  receipt = builtins.fromJSON (builtins.readFile ./source-validation.json);
  names = [ "CMakeLists.txt" "Info.plist" "PDGPUPreflight.cpp" "PDGPUPreflight.h" "preflight.h" "board.c" "observe.c" ];
  copied = pkgs.runCommand "gpu-preflight-source" {} ''
    mkdir -p "$out"
    cp ${./CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./Info.plist} "$out/Info.plist"
    cp ${./PDGPUPreflight.cpp} "$out/PDGPUPreflight.cpp"
    cp ${./PDGPUPreflight.h} "$out/PDGPUPreflight.h"
    cp ${./preflight.h} "$out/preflight.h"
    cp ${./board.c} "$out/board.c"
    cp ${./observe.c} "$out/observe.c"
  '';
  checks = pkgs.lib.concatMapStringsSep "\n" (name:
    let sha = receipt.files_sha256.${name};
    in assert builtins.match "[0-9a-f]{64}" sha != null;
    "echo '${sha}  src/Kernel/Extensions/PDGPUPreflight/${name}' | sha256sum -c -"
  ) names;
in assert receipt.revision == 1;
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "puredarwin-gpu-preflight";
  src = pkgs.runCommand "puredarwin-gpu-preflight-source" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/PDGPUPreflight"
    cp -R ${copied} "$out/src/Kernel/Extensions/PDGPUPreflight"
    printf '\nadd_subdirectory(PDGPUPreflight)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  postPatch = (old.postPatch or "") + "\n" + checks;
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix PDGPUPreflight.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/PDGPUPreflight/PDGPUPreflight.kext
    test -f "$bundle/Contents/MacOS/PDGPUPreflight"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
