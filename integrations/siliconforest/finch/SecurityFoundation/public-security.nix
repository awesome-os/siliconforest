# SPDX-License-Identifier: MIT
# Published Apple OSS source, not an SDK. AuthorizationPlugin is public source.
let e = import ../environment.nix;
in e.pkgs.fetchzip {
  url = "https://github.com/apple-oss-distributions/Security/archive/refs/tags/Security-61901.101.4.tar.gz";
  sha256 = "178y5851w0ynyrmyw98w0x6r1ysz35bgs0dcb2z0jfwgrv1bninn";
}
