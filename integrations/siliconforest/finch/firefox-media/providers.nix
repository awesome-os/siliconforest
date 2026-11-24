# SPDX-License-Identifier: MIT
# Media owner's versioned Firefox support payload. FX supplies ONE matching
# IOSurface/CGL/broker cohort and owns launcher/manifest activation.
let
 e=import ../environment.nix;
 cv=import ../VideoToolbox/zerocopy/corevideo.nix;
 producer=import ../VideoToolbox/zerocopy/build.nix;
 vt=import ../QuickTime/vt-queries.nix {provider=producer;};
in e.pkgs.runCommand "siliconforest-firefox-media-provider-payload-arm64" {} ''
 mkdir -p $out/Frameworks
 cp -R ${cv}/System/Library/Frameworks/CoreVideo.framework $out/Frameworks/
 cp -R ${vt}/System/Library/Frameworks/VideoToolbox.framework $out/Frameworks/
 cat > $out/providers.json <<'PROVIDERS'
 ${builtins.toJSON {
   CoreVideo=toString cv;
   VideoToolbox=toString vt;
   VideoToolboxProducer=toString producer;
   AudioToolbox="Installed canonical family retained; no replacement in this payload";
   IOSurfaceCGL="CODEX-FX supplies one coherent cohort and owns activation";
   status="Build only until cold prefix and installed-bundle qualification";
   zeroCPUCopy=false;
   kernelImportQualification="Check the running kernel and native strict capture result; package metadata cannot establish capability";
 }}
 PROVIDERS
''
