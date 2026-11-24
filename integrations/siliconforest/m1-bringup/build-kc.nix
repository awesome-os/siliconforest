# Increment009 attempt001 concrete kernel and separately compiled platform.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in
pkgs.callPackage ./kc-bringup.nix {
  inherit pkgs;
  kcTools = pd.inputs.kc-tools.packages.x86_64-linux.default;
  kernel = /nix/store/vnvmpk43am2q49h0wf7ajgg4zxdj48dm-puredarwin-kernel-arm64-t8103-bringup-0.1;
  kernelSource = /nix/store/a1lrkmvpxciil4fr087wj8zg33vqdma8-source;
  genericKexts = pd.packages.x86_64-linux.kexts-arm64;
  platformKexts = /nix/store/7s13ygyk7av9b5ryydvqzx13x82sbiph-puredarwin-m1-bringup-platform-0.1;
  kcRecipe = pd.outPath + "/nix/pkgs/toolchain/kc-arm64.nix";
}
