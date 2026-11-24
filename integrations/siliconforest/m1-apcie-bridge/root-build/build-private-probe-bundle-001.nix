# SDK bundle only. Run only after the frozen observer source is independently
# admitted. This expression neither selects a KC nor stages a boot packet.
let
  pin = "e7aa00a60f0b56ef35aa24252e81467787bf71ea";
  pd = (import ../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  providerRecipe = ./build-private-policy-objects-001.nix;
  provider = import providerRecipe;
  receiptText = builtins.readFile ../native_private_probe/source-validation-001.json;
  receipt = builtins.fromJSON receiptText;
  cmake = ./CMakeLists-private-probe-001.txt;
  files = [
    { name = "observer/PDBCMPrivateProbe.cpp"; path = ../native_private_probe/PDBCMPrivateProbe.cpp; sha = receipt.files_sha256."PDBCMPrivateProbe.cpp"; }
    { name = "observer/PDBCMPrivateProbe.h"; path = ../native_private_probe/PDBCMPrivateProbe.h; sha = receipt.files_sha256."PDBCMPrivateProbe.h"; }
    { name = "observer/probe.c"; path = ../native_private_probe/probe.c; sha = receipt.files_sha256."probe.c"; }
    { name = "observer/probe.h"; path = ../native_private_probe/probe.h; sha = receipt.files_sha256."probe.h"; }
    { name = "observer/protocol.c"; path = ../native_private_probe/protocol.c; sha = receipt.files_sha256."protocol.c"; }
    { name = "observer/protocol.h"; path = ../native_private_probe/protocol.h; sha = receipt.files_sha256."protocol.h"; }
    { name = "Info.plist"; path = ../native_private_probe/Info.plist; sha = receipt.files_sha256."Info.plist"; }
  ];
  source = pkgs.runCommand "puredarwin-private-probe-bundle-source-001" {} ''
    mkdir -p "$out/observer"
    cp ${cmake} "$out/CMakeLists.txt"
    ${pkgs.lib.concatMapStringsSep "\n" (f: ''
      cp ${f.path} "$out/${f.name}"
      echo '${f.sha}  '$out'/${f.name}' | sha256sum -c -
    '') files}
    cp -R ${provider}/share/inputs/. "$out/"
    cp ${provider}/lib/private_policy_provider.o "$out/private_policy_provider.o"
    echo '38120059ad53f0bebaf72346e96f48aac5a84d57ea0e62c8e1999328b763aeac  '$out'/private_policy_provider.o' | sha256sum -c -
  '';
  frozen = pkgs.lib.concatMapStringsSep "\n" (f:
    "echo '${f.sha}  src/Kernel/Extensions/PDBCMPrivateProbe/${f.name}' | sha256sum -c -"
  ) files;
in
assert pd.rev == pin;
assert builtins.hashString "sha256" receiptText == "71e7e8456216ef85ece55419637096af5f4be08dcc13323d87d86a98041bdb70";
assert builtins.hashFile "sha256" providerRecipe == "3f0d481f871493697e908611547dd78ba86663adaf4afc6e7b1442e2f66da8a2";
assert builtins.hashFile "sha256" cmake == "b84bee5adc12148d5fa67286a8bd8e986e3ea4a80b101630dc05ea9da70119ba";
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "puredarwin-private-probe-bundle-001";
  src = pkgs.runCommand "puredarwin-private-probe-sdk-tree-001" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/PDBCMPrivateProbe"
    cp -R ${source} "$out/src/Kernel/Extensions/PDBCMPrivateProbe"
    printf '\nadd_subdirectory(PDBCMPrivateProbe)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  postPatch = (old.postPatch or "") + "\n" + frozen + "\n" + ''
    echo '38120059ad53f0bebaf72346e96f48aac5a84d57ea0e62c8e1999328b763aeac  src/Kernel/Extensions/PDBCMPrivateProbe/private_policy_provider.o' | sha256sum -c -
  '';
  postConfigure = (old.postConfigure or "") + ''
    cmake -S . -B build-nix -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix PDBCMPrivateProbe.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/PDBCMPrivateProbe/PDBCMPrivateProbe.kext
    test -f "$bundle/Contents/MacOS/PDBCMPrivateProbe"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    mkdir -p "$out/share/inputs" "$out/share/validation"
    cp -R ${source}/. "$out/share/inputs/"
    cp ${provider}/share/build-inputs.json "$out/share/provider-build-inputs.json"
    cp ${../native_private_probe/source-validation-001.json} "$out/share/observer-source-validation.json"
    cp build-nix/compile_commands.json "$out/share/validation/"
    runHook postInstall
  '';
  dontStrip = true;
})
