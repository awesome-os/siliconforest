# The pinned SiliconForest flake (same pin as every other recipe in this lab) and its nixpkgs.
# Everything here, including the Rust toolchain, comes out of these two.
rec {
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  # arm64-apple-darwin20.4-{clang,ld,nm,otool,objdump}: what the tree's kexts are built with
  crossToolchain = pd.packages.x86_64-linux.arm64-cross-toolchain;
  appleSdk = pd.packages.x86_64-linux.apple-sdk;
}
