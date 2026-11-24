# Gate build for the port-0 link stage (slice 3): the slice-1 PDApcieT8103Bridge bundle
# plus bringup.c and linkup.c, compiled with the SDK as kernel code, so the binary can be
# checked for the write gate. It builds one bundle only. It does not create,
# modify or link any kernel collection, and it stages nothing. board.c and
# survey.h are the frozen m1-dart-survey revision-2 files, hash-checked here.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  survey = builtins.fromJSON (builtins.readFile ../m1-dart-survey/source-validation.json);
  copied = pkgs.runCommand "puredarwin-apcie-bridge-linkup-gate-source" {} ''
    mkdir -p "$out"
    cp ${./CMakeLists-linkup.txt} "$out/CMakeLists.txt"
    cp ${./Info.plist} "$out/Info.plist"
    cp ${./PDApcieT8103Bridge.cpp} "$out/PDApcieT8103Bridge.cpp"
    cp ${./PDApcieT8103Bridge.h} "$out/PDApcieT8103Bridge.h"
    cp ${./observe.c} "$out/observe.c"
    cp ${./observe.h} "$out/observe.h"
    cp ${./observe_layout.h} "$out/observe_layout.h"
    cp ${./bringup.c} "$out/bringup.c"
    cp ${./bringup.h} "$out/bringup.h"
    cp ${./NOTICE} "$out/NOTICE"
    cp ${./NOTICE-bringup} "$out/NOTICE-bringup"
    cp ${./linkup.c} "$out/linkup.c"
    cp ${./linkup.h} "$out/linkup.h"
    cp ${./NOTICE-linkup} "$out/NOTICE-linkup"
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
  pname = "puredarwin-apcie-bridge-linkup-gate";
  src = pkgs.runCommand "puredarwin-apcie-bridge-linkup-gate-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/PDApcieT8103Bridge"
    cp -R ${copied} "$out/src/Kernel/Extensions/PDApcieT8103Bridge"
    printf '\nadd_subdirectory(PDApcieT8103Bridge)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  postPatch = (old.postPatch or "") + "\n" + frozen;
  buildPhase = ''
    runHook preBuild
    # Both engine compiles must define KERNEL and must not define the fake macro.
    for f in bringup.c linkup.c; do
      cmd=$(ninja -C build-nix -t commands PDApcieT8103Bridge.kext | grep -F "PDApcieT8103Bridge/$f")
      case "$cmd" in *-DKERNEL*) echo "$f: KERNEL defined" ;; *) echo "$f: KERNEL missing"; exit 1 ;; esac
      case "$cmd" in *APB_FAKE_REGISTER_FILE*) echo "$f: fake macro present"; exit 1 ;; *) echo "$f: no fake macro" ;; esac
    done
    ninja -C build-nix PDApcieT8103Bridge.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/PDApcieT8103Bridge/PDApcieT8103Bridge.kext
    test -f "$bundle/Contents/MacOS/PDApcieT8103Bridge"
    mkdir -p "$out/System/Library/Extensions" "$out/objects"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    cp build-nix/src/Kernel/Extensions/PDApcieT8103Bridge/CMakeFiles/PDApcieT8103Bridge.kext.dir/bringup.c.o "$out/objects/"
    cp build-nix/src/Kernel/Extensions/PDApcieT8103Bridge/CMakeFiles/PDApcieT8103Bridge.kext.dir/linkup.c.o "$out/objects/"
    runHook postInstall
  '';
})
