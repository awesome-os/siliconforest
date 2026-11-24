# SPDX-License-Identifier: MIT
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs crossToolchain appleSdk pd;
in pkgs.runCommand "siliconforest-ext4-eof-qemu-io-test" {} ''
  mkdir -p "$out/bin"
  ${crossToolchain}/bin/arm64-apple-darwin20.4-clang -arch arm64 -O2 -Wall -Wextra -Werror \
    --ld-path=${pd.packages.x86_64-linux.native-ld}/bin/ld \
    -isysroot ${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    ${./eof-io.c} -o "$out/bin/eof-io"
''
