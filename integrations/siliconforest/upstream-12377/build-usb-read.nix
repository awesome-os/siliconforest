# SPDX-License-Identifier: MIT
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs pd crossToolchain appleSdk;
in pkgs.runCommand "siliconforest-upstream-qemu-usb-read" {} ''
  mkdir -p "$out/bin"
  ${crossToolchain}/bin/arm64-apple-darwin20.4-clang -arch arm64 -O2 -Wall -Wextra -Werror \
    -D_DARWIN_C_SOURCE -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
    --ld-path=${pd.packages.x86_64-linux.native-ld}/bin/ld \
    -isysroot ${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    ${./usb-read.c} -o "$out/bin/usb-read"
''
