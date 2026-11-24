# SDK compile of the read-only apcie,t8103 port-0 bridge skeleton as a standalone
# kext bundle. Same shape as m1-dart-survey/build-survey-rev002.nix. It builds one
# bundle only: it does not create, modify or link any kernel collection, and it
# stages nothing. board.c and survey.h are the frozen m1-dart-survey revision-2
# files, checked against that leaf's own source-validation.json.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  survey = builtins.fromJSON (builtins.readFile ../m1-dart-survey/source-validation.json);
  copied = pkgs.runCommand "puredarwin-apcie-bridge-observe-source" {} ''
    mkdir -p "$out"
    cp ${./CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./Info.plist} "$out/Info.plist"
    cp ${./PDApcieT8103Bridge.cpp} "$out/PDApcieT8103Bridge.cpp"
    cp ${./PDApcieT8103Bridge.h} "$out/PDApcieT8103Bridge.h"
    cp ${./observe.c} "$out/observe.c"
    cp ${./observe.h} "$out/observe.h"
    cp ${./observe_layout.h} "$out/observe_layout.h"
    cp ${./NOTICE} "$out/NOTICE"
    cp ${../m1-dart-survey/board.c} "$out/board.c"
    cp ${../m1-dart-survey/survey.h} "$out/survey.h"
  '';
  frozen = pkgs.lib.concatMapStringsSep "\n" (name:
    let sha = survey.files_sha256.${name};
    in assert builtins.match "[0-9a-f]{64}" sha != null;
    "echo '${sha}  src/Kernel/Extensions/PDApcieT8103Bridge/${name}' | sha256sum -c -"
  ) [ "board.c" "survey.h" ];
in assert survey.revision == 2;
assert survey.source_set_sha256 == "b773df5931aa1e5272ef7ca9f673684abff2dd89cc60f05f3662e894cf30b686";
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "puredarwin-apcie-bridge-observe";
  src = pkgs.runCommand "puredarwin-apcie-bridge-observe-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/PDApcieT8103Bridge"
    cp -R ${copied} "$out/src/Kernel/Extensions/PDApcieT8103Bridge"
    printf '\nadd_subdirectory(PDApcieT8103Bridge)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  postPatch = (old.postPatch or "") + "\n" + frozen;
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix PDApcieT8103Bridge.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/PDApcieT8103Bridge/PDApcieT8103Bridge.kext
    test -f "$bundle/Contents/MacOS/PDApcieT8103Bridge"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
