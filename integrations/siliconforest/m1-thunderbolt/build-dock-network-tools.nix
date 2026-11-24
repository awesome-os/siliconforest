# SPDX-License-Identifier: MIT
# Dedicated interface-only dock tools; shared Wi-Fi tools stay unchanged.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-dock-network-tools-0.1";
  dontUnpack = true;
  nativeBuildInputs = [ pkgs.python3 ];
  buildPhase = ''
    python3 ${./tools/prepare_dock_network.py} \
      ${../m1-bwfm/dhcp/sf-dhcp.c} \
      ${/home/chrisk/src/darwin-lab/sources/puredarwin-m1-firstboot/src/Userspace/netsetup/netsetup.c} .
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    for tool in sf-dock-dhcp sf-dock-netsetup; do
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
        -std=c11 -Wall -Wextra -O1 -isystem "$DARWIN_SDK_ROOT/usr/include" \
        "$tool.c" --ld-path=${p.native-ld}/bin/ld -nostdlib \
        -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system \
        -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
        -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains -Wl,-dead_strip \
        -lSystem -o "$tool"
    done
  '';
  installPhase = ''
    mkdir -p "$out/bin"
    cp sf-dock-dhcp sf-dock-netsetup "$out/bin/"
  '';
  dontFixup = true;
  dontStrip = true;
}
