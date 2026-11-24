# SPDX-License-Identifier: MIT
# Native helper only; does not open a target or submit work during the build.
let
 inherit (import ../m1-rust-kext/pins.nix) pkgs crossToolchain appleSdk pd;
 drm = pd.packages.x86_64-linux.libdrm-dri-arm64;
 header = ../../../sources/gpu-plan/linux-aurora/include/uapi/drm/asahi_drm.h;
in assert builtins.hashFile "sha256" header == "b57f2d88995fd2d0ad6d7b11284a923b1ca18afd2f6a66c4e490606ce463ac90";
pkgs.runCommand "siliconforest-gpu-bo-upl-pin-probe" {} ''
 mkdir include
 cp ${header} include/asahi_drm.h
 ${crossToolchain}/bin/arm64-apple-darwin20.4-clang -arch arm64 -O2 -Wall -Wextra -Werror \
   --ld-path=${pd.packages.x86_64-linux.native-ld}/bin/ld \
   -isysroot ${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
   -Iinclude -I${drm}/include/libdrm -I${../m1-avd/include} -I${../m1-avd/user} \
   ${./pin-probe.c} ${../m1-avd/user/sfavd.c} -o pin-probe
 mkdir -p "$out/bin"
 cp pin-probe "$out/bin/"
''
