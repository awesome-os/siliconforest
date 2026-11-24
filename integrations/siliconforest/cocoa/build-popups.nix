# SPDX-License-Identifier: MIT
# CocoaPopups.app (./popups): the popup test application (menus, lists, tool tips) for arm64, built with upstream's own
# recipe for an AppKit app (nix/pkgs/apps/calculator.nix at the pinned commit, with its arm64
# dependency set) and our source in place of Calculator's. The recipe's file names are kept
# (main.m, Info.plist, *.lproj, Resources/calculator.svg as the icon); the product is renamed.
#   nix-build --extra-experimental-features 'nix-command flakes' build-hello.nix --no-out-link
let
  pd = import ./pin.nix;
  calc = pd."calculator-msm-arm64";
in (calc.override { src = ./popups; }).overrideAttrs (old: {
  pname = "siliconforest-cocoa-popups";
  installPhase = ''
    app="$out/Applications/CocoaPopups.app/Contents"
    install -Dm755 Calculator "$app/MacOS/CocoaPopups"
    install -Dm644 Info.plist "$app/Info.plist"
    for l in *.lproj; do install -Dm644 -t "$app/Resources/$l" $l/*.strings; done
    install -Dm644 AppIcon.png "$app/Resources/AppIcon.png"
  '';
})
