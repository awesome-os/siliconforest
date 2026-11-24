# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  security = import ../Security/build.nix;
  sf = import ../SecurityFoundation/build.nix;
  publicSecurity = import ../SecurityFoundation/public-security.nix;
  appkit = e.p.appkit-msm-arm64;
in import ../objc-framework.nix {
  name = "SecurityInterface";
  arc = false;
  inherit security;
  extraIncludes = "-F${security}/System/Library/Frameworks -F${sf}/System/Library/Frameworks -I${publicSecurity}/OSX/libsecurity_authorization/lib -I${appkit}/usr/include -I${e.pd.outPath}/src/Frameworks -I${e.pd.outPath}/src/Frameworks/OpenGL/include -I${e.p.mesa-msm-arm64}/usr/include -I${e.p.onyx2d-arm64}/usr/include -I${e.p.coregraphics-arm64}/usr/include";
  extraLinks = "-F${appkit}/System/Library/Frameworks -framework AppKit -F${sf}/System/Library/Frameworks -Wl,-reexport_framework,SecurityFoundation -Wl,-dylib_file,@loader_path/Security.pd:${security}/System/Library/Frameworks/Security.framework/Versions/A/Security.pd";
}
