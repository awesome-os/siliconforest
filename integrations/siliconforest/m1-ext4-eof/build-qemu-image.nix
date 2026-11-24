# SPDX-License-Identifier: MIT
# Isolated virt image; swap only ext4 for the exact frozen or EOF-fixed M1 SDK.
{ fixed ? true }:
let
  inherit (import ../rebase/pd.nix) pkgs;
  base = import ../qemu-dev/build-image.nix { kernelConfig = "debug"; };
  kc = import ../qemu-dev/build-kc.nix { kernelConfig = "debug"; };
  ext4 = if fixed then import ./build-ext4.nix else builtins.storePath "/nix/store/11f479qxfnb83zhnr60fgj5bk4f2d1s1-puredarwin-ext4-downgrade-0.1";
in base.override (old: {
  kc = kc.override (k: {
    kexts = pkgs.runCommand (if fixed then "qemu-eof-fixed-kexts" else "qemu-eof-frozen-kexts") {} ''
      mkdir -p "$out"
      cp -rs ${k.kexts}/. "$out/"
      # chmod only our new directories; never follow an input-store link.
      test ! -L "$out/System"
      test ! -L "$out/System/Library"
      test ! -L "$out/System/Library/Extensions"
      find "$out" -type d -exec chmod u+w {} +
      test -f "$out/System/Library/Extensions/ext4.kext/Contents/MacOS/ext4"
      rm -rf "$out/System/Library/Extensions/ext4.kext"
      ln -s ${ext4}/System/Library/Extensions/ext4.kext "$out/System/Library/Extensions/ext4.kext"
    '';
  });
})
