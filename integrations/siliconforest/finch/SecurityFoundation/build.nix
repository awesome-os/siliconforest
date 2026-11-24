# SPDX-License-Identifier: MIT
let
  declarations = import ../Security/build.nix;
in import ../objc-framework.nix {
  name = "SecurityFoundation";
  arc = false;
  security = declarations;
  extraLinks = "-Wl,-dylib_file,@loader_path/Security.pd:${declarations}/System/Library/Frameworks/Security.framework/Versions/A/Security.pd";
  extraIncludes = "-F${declarations}/System/Library/Frameworks";
}
