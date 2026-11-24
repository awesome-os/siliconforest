# SPDX-License-Identifier: MIT
# Port the small upstream recipe; no upstream flake/Nix reorganisation.
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs pd crossToolchain appleSdk;
  source = pkgs.applyPatches {
    name = "siliconforest-sfres-selected-source";
    src = pd.outPath;
    patches = [ ./patches/0163b2db-sfres.patch ];
  };
  p = pd.packages.x86_64-linux;
in pkgs.callPackage (source + "/nix/pkgs/apps/sfres.nix") {
  darwinCrossToolchain = crossToolchain;
  nativeLd = p.native-ld;
  libSystem = p.libsystem-arm64;
  libobjc = p.libobjc-arm64;
  corefoundation = p.coreFoundation-arm64;
  sfsurface = p.sfsurface-arm64;
  coregraphics = p.coregraphics-arm64;
  inherit appleSdk;
  src = source + "/src/Userspace/sfres";
  targetTriple = "arm64-apple-darwin20.4";
}
