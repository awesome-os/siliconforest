# Isolated IOHIDFamily build. No checkout mutation and no target execution.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  manifest = builtins.fromJSON (builtins.readFile ./revisions/002/manifest.json);
  files = [
    "src/Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDEventService.cpp"
    "src/Kernel/Extensions/IOHIDFamily/IOHIDSystem/SFHIDEventQueue.cpp"
    "src/Kernel/Extensions/IOHIDFamily/IOHIDSystem/SFHIDEventQueue.h"
  ];
  checks = field: pkgs.lib.concatMapStringsSep "\n" (name:
    let sha = manifest.files.${name}.${field};
    in assert builtins.match "[0-9a-f]{64}" sha != null;
      ''echo '${sha}  ${name}' | sha256sum -c -''
  ) files;
  # SiliconForest names: the patch is ./revisions/002/candidate.patch (frozen, hash-bound by its
  # manifest) with the rename applied (tools/sf_rename.py SUCCESSORS). The manifest's per-file
  # hashes are of upstream's text before the rename and are not compared here; the patch must
  # still apply exactly.
  renamed = ./hid-publication-rev002.patch;
in
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-native-hid-publication-rev002";
  patches = (old.patches or []) ++ [ renamed ];
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix IOHIDFamily.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/IOHIDFamily/IOHIDFamily.kext
    test -f "$bundle/Contents/MacOS/IOHIDFamily"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
