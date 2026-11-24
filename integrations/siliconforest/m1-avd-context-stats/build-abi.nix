# SPDX-License-Identifier: MIT
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs crossToolchain appleSdk pd;
  candidate = import ./build-kext.nix;
  frozen = builtins.storePath "/nix/store/yw38khclrlqp2a7hric4i314ckcjs2in-puredarwin-firefox-avd-import-tree";
in pkgs.runCommand "siliconforest-avd-context-stats-abi" {} ''
  mkdir -p "$out/bin"
  for phase in before after; do
    if test "$phase" = before; then
      headers=${frozen}/src/Kernel/Extensions/SFAppleAVD/include
    else
      headers=${candidate}/include
    fi
    ${crossToolchain}/bin/arm64-apple-darwin20.4-clang -arch arm64 -O2 -Wall -Wextra -Werror \
      --ld-path=${pd.packages.x86_64-linux.native-ld}/bin/ld \
      -isysroot ${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
      -I"$headers" ${./abi.c} -o "$out/bin/abi-$phase"
  done
  # Target binaries are cross-compiled only, never executed here.
''
