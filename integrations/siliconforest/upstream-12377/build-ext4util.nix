# SPDX-License-Identifier: MIT
{ fixed ? true }:
let
  inherit (import ../m1-rust-kext/pins.nix) pkgs pd crossToolchain appleSdk;
  source = pkgs.runCommand "siliconforest-upstream-ext4util-source" { nativeBuildInputs = [ pkgs.patch ]; } ''
    mkdir -p "$out/src/Userspace/ext4util"
    cp ${pd.outPath}/src/Userspace/ext4util/ext4.util.c "$out/src/Userspace/ext4util/"
    chmod u+w "$out/src/Userspace/ext4util/ext4.util.c"
    cd "$out"
    ${pkgs.lib.optionalString fixed "patch --fuzz=0 -p1 < ${./patches/ca1fd5ed-ext4util.patch}"}
  '';
in pkgs.runCommand (if fixed then "siliconforest-ext4util-upstream-block-read" else "siliconforest-ext4util-frozen-block-read") {} ''
  mkdir -p "$out/System/Library/Filesystems/ext4.fs/Contents/Resources"
  ${crossToolchain}/bin/arm64-apple-darwin20.4-clang -arch arm64 -O2 -Wall -Wextra \
    -D_DARWIN_C_SOURCE -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -fno-stack-protector \
    --ld-path=${pd.packages.x86_64-linux.native-ld}/bin/ld \
    -isysroot ${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    -I${pd.outPath}/src/Kernel/xnu/bsd \
    ${source}/src/Userspace/ext4util/ext4.util.c \
    -o "$out/System/Library/Filesystems/ext4.fs/Contents/Resources/ext4.util"
  cp ${pd.outPath}/src/Userspace/ext4util/ext4.fs/Contents/Info.plist "$out/System/Library/Filesystems/ext4.fs/Contents/"
''
