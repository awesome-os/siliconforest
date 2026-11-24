# Applies the userspace part of 0002 to the pinned tree and builds the sf-dhcp target in-tree.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  system = import ../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
  selected = pd.packages.x86_64-linux.userland-arm64.override {
    pname = "siliconforest-pd-dhcp-from-patch";
    buildTargets = [ "sf-dhcp" ];
    prebuiltLibSystem = system;
    xorgDriverIncludes = null;
    installUserlandTargetsOnly = true;
  };
in selected.overrideAttrs (old: {
  src = pkgs.runCommand "siliconforest-pd-dhcp-tree" { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    cd "$out"
    # the nix registry file is not part of this filtered source; only the Userspace hunks apply
    patch -p1 -f < ${./0002-m1-pcie-wifi-userspace-and-registry.patch} || true
    test -f src/Userspace/sf-dhcp/sf-dhcp.c
    grep -q 'add_subdirectory(sf-dhcp)' src/Userspace/CMakeLists.txt
  '';
  installPhase = ''
    mkdir -p "$out/usr/bin"
    cp build-nix/src/Userspace/sf-dhcp/sf-dhcp "$out/usr/bin/"
  '';
  dontFixup = true;
  dontStrip = true;
})
