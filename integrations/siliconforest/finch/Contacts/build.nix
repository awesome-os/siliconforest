# SPDX-License-Identifier: MIT
let e = import ../environment.nix;
in import ../objc-framework.nix {
  name = "Contacts";
  extraIncludes = "-I${e.p.coregraphics-arm64}/usr/include -I${e.p.onyx2d-arm64}/usr/include";
  extraLinks = "-L${e.p.coregraphics-arm64}/usr/lib -lCoreGraphics";
}
