# SPDX-License-Identifier: MIT
# Experimental replacement of RavynXHCIPort; not part of the daily collection.
let
  source = import ../rebase/pd.nix;
  inherit (source) pd pkgs;
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-apple-xhci";
  src = pkgs.runCommand "siliconforest-apple-xhci-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    cd "$out"
    patch -p1 < ${./patches/apple-dma-lifetime.patch}
    dest="$out/src/Kernel/Extensions/RavynXHCIPort"
    cp ${./kernel}/sf_usb_* "$dest/"
    cp ${../m1-dcp-dart/include/sf_dart_table.h} "$dest/sf_dart_table.h"
    cp ${../m1-dcp-dart/src/sf_dart_table.c} "$dest/sf_dart_table.c"
    cp ${../m1-sound/kernel}/sf_soc.* "$dest/"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix RavynXHCIPort.kext
    runHook postBuild
  '';
  installPhase = ''
    mkdir -p "$out/System/Library/Extensions"
    cp -R build-nix/src/Kernel/Extensions/RavynXHCIPort/RavynXHCIPort.kext "$out/System/Library/Extensions/"
    cp ${./NOTICE} "$out/System/Library/Extensions/RavynXHCIPort.kext/Contents/NOTICE"
  '';
})
