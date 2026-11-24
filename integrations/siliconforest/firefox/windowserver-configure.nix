# SPDX-License-Identifier: MIT
# Private protocol-race diagnostic, never selected or installed by this recipe.
let
  p = import ../cocoa/patched.nix;
  a = import ../cocoa/arm64.nix;
  src = a.pkgs.runCommand "sffx-windowserver-configure-order-source" {} ''
    cp -r ${p.windowserverSrc} $out
    chmod -R u+w $out
    cd $out
    ${a.pkgs.patch}/bin/patch --batch --fuzz=0 -p1 < ${./cocoa-patches/0240-windowserver-configure-ack-and-unmap-order.patch}
  '';
in p.windowserver.override { inherit src; }
