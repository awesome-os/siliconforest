# Shared inputs for the Mesa asahi port: the SiliconForest flake at the lab's pin, its nixpkgs, and
# the Mesa source the M1's Linux runs (omacom/mesa integration/aurora 147a62f6190, 26.3.0-devel).
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in rec {
  inherit pd pkgs;
  p = pd.packages.x86_64-linux;
  mesaRev = "147a62f619001f5f2588c7417c2b1fac3b0d93ad";
  mesaVersion = "26.3.0-devel-147a62f6190";
  mesaSrc = pkgs.fetchurl {
    name = "mesa-147a62f6190.tar.gz";
    url = "https://github.com/omacom/mesa/archive/${mesaRev}.tar.gz";
    sha256 = "15b27d00ba2d857837001d357f189e64809b27937eba0dd722c9d28ec4ad0dcf";
  };
  pythonEnv = pkgs.python3.withPackages (ps: [ ps.mako ps.pyyaml ps.packaging ps.setuptools ]);
  triple = "arm64-apple-darwin20.4";
  sdkRoot = "${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
}
