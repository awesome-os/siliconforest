# SPDX-License-Identifier: MIT
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs crossToolchain appleSdk pd;
in pkgs.runCommand "siliconforest-killable-kill-probe" {} ''
  ${crossToolchain}/bin/arm64-apple-darwin20.4-clang -arch arm64 -O2 -Wall -Wextra -Werror \
    --ld-path=${pd.packages.x86_64-linux.native-ld}/bin/ld \
    -isysroot ${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    ${./kill-probe.c} -o kill-probe
  mkdir -p "$out/bin"
  cp kill-probe "$out/bin/"
''
