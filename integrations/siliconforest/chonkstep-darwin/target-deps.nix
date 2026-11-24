let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  xkb = p.xkbcommon-arm64.override {
    withX11 = false;
    libxcb = null;
    libXau = null;
    libXdmcp = null;
  };
in pkgs.symlinkJoin {
  name = "siliconforest-chonkstep-target-deps";
  paths = [ p.iokit-arm64 xkb ];
}
