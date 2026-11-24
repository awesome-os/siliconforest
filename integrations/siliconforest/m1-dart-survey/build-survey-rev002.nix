# Isolated, hash-bound read-only diagnostic bundle. No target execution.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  receipt = builtins.fromJSON (builtins.readFile ./source-validation.json);
  names = [ "CMakeLists.txt" "Info.plist" "PDDARTSurvey.cpp" "PDDARTSurvey.h" "observation_layout.h" "board.c" "survey.c" "survey.h" "NOTICE-m1n1.txt" ];
  copied = pkgs.runCommand "puredarwin-dart-survey-frozen-source" {} ''
    mkdir -p "$out"
    cp ${./CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./Info.plist} "$out/Info.plist"
    cp ${./PDDARTSurvey.cpp} "$out/PDDARTSurvey.cpp"
    cp ${./PDDARTSurvey.h} "$out/PDDARTSurvey.h"
    cp ${./observation_layout.h} "$out/observation_layout.h"
    cp ${./board.c} "$out/board.c"
    cp ${./survey.c} "$out/survey.c"
    cp ${./survey.h} "$out/survey.h"
    cp ${./NOTICE-m1n1.txt} "$out/NOTICE-m1n1.txt"
  '';
  checks = pkgs.lib.concatMapStringsSep "\n" (name:
    let sha = receipt.files_sha256.${name};
    in assert builtins.match "[0-9a-f]{64}" sha != null;
    "echo '${sha}  src/Kernel/Extensions/PDDARTSurvey/${name}' | sha256sum -c -"
  ) names;
in assert receipt.revision == 2;
assert receipt.source_set_sha256 == "b773df5931aa1e5272ef7ca9f673684abff2dd89cc60f05f3662e894cf30b686";
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "puredarwin-dart-survey-rev002";
  src = pkgs.runCommand "puredarwin-dart-survey-source" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/PDDARTSurvey"
    cp -R ${copied} "$out/src/Kernel/Extensions/PDDARTSurvey"
    printf '\nadd_subdirectory(PDDARTSurvey)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  postPatch = (old.postPatch or "") + "\n" + checks;
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix PDDARTSurvey.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/PDDARTSurvey/PDDARTSurvey.kext
    test -f "$bundle/Contents/MacOS/PDDARTSurvey"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
