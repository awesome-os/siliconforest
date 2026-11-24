# SPDX-License-Identifier: MIT
let a=import ../cocoa/arm64.nix;
 support=a.pkgs.runCommand "sfsc-notification-sources" {} ''
   mkdir -p $out
   cp ${./sc-route-source.c} $out/sc-route-source.c
   cp ${./sc-route-source.h} $out/sc-route-source.h
   cp ${./sc-reach-callbacks.inc} $out/sc-reach-callbacks.inc
   cp ${./sc-store-network.c} $out/sc-store-network.c
 '';
in a.pd.systemConfiguration-arm64.overrideAttrs (old: {
 pname="siliconforest-systemconfiguration-network-notifications";
 nativeBuildInputs=(old.nativeBuildInputs or [])++[a.pkgs.python3];
 postPatch=(old.postPatch or "")+''
   ${a.pkgs.python3}/bin/python3 ${./sc-network-patch.py} . ${support}
 '';
 buildPhase=''
   export LIBTOOL=${a.pkgs.llvmPackages.llvm}/bin/llvm-libtool-darwin
   runHook preBuild
   cmake --build build-nix --target SystemConfiguration -j$NIX_BUILD_CORES
   runHook postBuild
 '';
 installPhase=''
   fw="$out/System/Library/Frameworks/SystemConfiguration.framework"
   mkdir -p "$fw/Versions/A/Headers" "$fw/Versions/A/Resources"
   cp build-nix/src/Libraries/SystemConfiguration/libSystemConfiguration.dylib "$fw/Versions/A/SystemConfiguration"
   cp -a src/Libraries/SystemConfiguration/include/SystemConfiguration/. "$fw/Versions/A/Headers/"
   cp src/Libraries/SystemConfiguration/SystemConfiguration.fproj/{Info.plist,NetworkConfiguration.plist} "$fw/Versions/A/Resources/"
   cp -a src/Libraries/SystemConfiguration/SystemConfiguration.fproj/en.lproj "$fw/Versions/A/Resources/"
   ln -s A "$fw/Versions/Current"
   for part in SystemConfiguration Headers Resources; do ln -s "Versions/Current/$part" "$fw/$part"; done
 '';
})
