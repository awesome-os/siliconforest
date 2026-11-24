# SPDX-License-Identifier: MIT
# Same reviewed VT producer/query wrapper with TERM's clock + CV lifetime.
# The browser still supplies ONE FX-selected IOSurface/CGL/broker family.
let
 e=import ../environment.nix;
 base=import ./providers.nix;
 cv=import ./clocked-corevideo.nix;
in e.pkgs.runCommand "siliconforest-firefox-clocked-media-payload-arm64" {} ''
 mkdir -p $out/Frameworks
 cp -R ${base}/Frameworks/VideoToolbox.framework $out/Frameworks/
 cp -R ${cv}/System/Library/Frameworks/CoreVideo.framework $out/Frameworks/
 cat > $out/providers.json <<'JSON'
 ${builtins.toJSON {VideoToolboxPayload=toString base;CoreVideo=toString cv;
   IOSurfaceCGL="Selected by FX runtime plan, never copied here";
   scope="Build only; native strict CLI and whole browser qualification are separate";}}
 JSON
''
