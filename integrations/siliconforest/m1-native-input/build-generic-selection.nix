let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  hid = import ./build-hid.nix;
in pkgs.runCommand "puredarwin-native-hid-generic-selection" {} ''
  set -eu
  dest="$out/System/Library/Extensions"
  mkdir -p "$dest"
  for bundle in ${pd.packages.x86_64-linux.kexts-arm64}/System/Library/Extensions/*.kext; do
    name=$(basename "$bundle")
    case "$name" in IOHIDFamily.kext) ;; *) ln -s "$bundle" "$dest/$name" ;; esac
  done
  replacement=${hid}/System/Library/Extensions/IOHIDFamily.kext
  test -f "$replacement/Contents/MacOS/IOHIDFamily"
  ln -s "$replacement" "$dest/IOHIDFamily.kext"
''
