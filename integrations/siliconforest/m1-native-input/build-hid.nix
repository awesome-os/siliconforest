# Isolated IOHIDFamily build. No checkout mutation and no target execution.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  manifest = builtins.fromJSON (builtins.readFile ./manifest.json);
  files = [
    "src/Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDEventService.cpp"
    "src/Kernel/Extensions/IOHIDFamily/IOHIDSystem/PDHIDEventQueue.cpp"
    "src/Kernel/Extensions/IOHIDFamily/IOHIDSystem/PDHIDEventQueue.h"
  ];
  checks = field: pkgs.lib.concatMapStringsSep "\n" (name:
    let sha = manifest.files.${name}.${field};
    in assert builtins.match "[0-9a-f]{64}" sha != null;
      ''echo '${sha}  ${name}' | sha256sum -c -''
  ) files;
in assert builtins.hashFile "sha256" ./candidate.patch == manifest.patches."candidate.patch";
pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "puredarwin-native-hid-publication";
  patches = (old.patches or []) ++ [ ./candidate.patch ];
  prePatch = (old.prePatch or "") + "\n" + checks "before_sha256";
  postPatch = (old.postPatch or "") + "\n" + checks "after_sha256";
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
