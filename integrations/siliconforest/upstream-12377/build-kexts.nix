# SPDX-License-Identifier: MIT
# Selected source ports on c713d8fb; no flake update or default collection edit.
let
  inherit (import ../rebase/pd.nix) pd pkgs;
  base = pd.packages.x86_64-linux.kexts-arm64;
in base.overrideAttrs (old: {
  pname = "siliconforest-selected-upstream-kexts";
  patches = (old.patches or []) ++ [
    ./patches/2617f566-fbsdkpi.patch
    ./patches/aaf3604d-usb.patch
    ./patches/a25f0365-pci-scan.patch
  ];
  buildPhase = ''
    runHook preBuild
    ninja -j6 -k0 -C build-nix IOPCIFamily IOUSBCompositeDriver.kext RavynXHCIPort.kext SFFBSDem.kext SFFBSDigc.kext
    runHook postBuild
  '';
  installPhase = ''
    mkdir -p "$out/System/Library/Extensions" "$out/nix-support"
    for name in IOPCIFamily IOUSBCompositeDriver RavynXHCIPort SFFBSDem SFFBSDigc; do
      bundle=$(find build-nix/src/Kernel/Extensions -type d -name "$name.kext" -print -quit)
      test -f "$bundle/Contents/MacOS/$name"
      cp -R "$bundle" "$out/System/Library/Extensions/"
    done
    cp ${./manifest.json} "$out/nix-support/upstream-manifest.json"
  '';
})
