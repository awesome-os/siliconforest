# SPDX-License-Identifier: MIT
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs crossToolchain appleSdk pd;
in pkgs.runCommand "siliconforest-devfs-clone-probe" {} ''
  ${crossToolchain}/bin/arm64-apple-darwin20.4-clang -arch arm64 -O2 -Wall -Wextra -Werror \
    --ld-path=${pd.packages.x86_64-linux.native-ld}/bin/ld \
    -isysroot ${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    ${./clone-probe.c} -o clone-probe
  mkdir -p "$out/bin"
  cp clone-probe "$out/bin/"
''
