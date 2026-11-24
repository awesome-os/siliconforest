# SPDX-License-Identifier: MIT
# The tree's arm64 Security.framework with patches/security-copy-certificate-chain.patch
# (SecTrustCopyCertificateChain, which every Go program built for macOS imports).
let
  pd = import ../qemu-dev/sf-userland.nix;   # the tip's Security needs that file's tree patch to build
in pd.packages.x86_64-linux.security-arm64.overrideAttrs (old: {
  pname = "siliconforest-security-homebrew";
  patches = (old.patches or []) ++ [ ./patches/security-copy-certificate-chain.patch ];
})
