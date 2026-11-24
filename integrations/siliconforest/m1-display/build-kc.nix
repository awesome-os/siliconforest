let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in
pkgs.callPackage ./kc-display.nix {
  inherit pkgs;
  kcTools = import ../m1-bringup/kc-tools-fixed.nix;
  kernel = /nix/store/7fjh48gkqgrhy4q29ji9c2nk5hdnr8h6-puredarwin-kernel-arm64-t8103-bringup-0.1;
  kernelSource = /nix/store/skqrs5jy91w4w560qga94hnhmvaxp6g2-source;
  genericKexts = pd.packages.x86_64-linux.kexts-arm64;
  platformKexts = /nix/store/7s13ygyk7av9b5ryydvqzx13x82sbiph-puredarwin-m1-bringup-platform-0.1;
  displayKexts = import ./build-framebuffer.nix;
  kcRecipe = pd.outPath + "/nix/pkgs/toolchain/kc-arm64.nix";
}
