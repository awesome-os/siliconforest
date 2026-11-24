# SPDX-License-Identifier: MIT
# Link input for the Foundation owner; does not build or install Foundation.
let
 e=import ../environment.nix;
 inherit(e) pkgs p;
 prefix=import ./build.nix;
 header=pkgs.runCommand "finch-networking-canonical-declarations" {nativeBuildInputs=[pkgs.python3];} ''
  mkdir -p $out
  python3 ${./canonical-header.py} ${./include}/SFNetworking.h $out/Networking.h
 '';
in prefix.overrideAttrs(old: {
 name="siliconforest-finch-networking-canonical-objects-arm64";
 buildPhase=pkgs.lib.replaceStrings ["-include ${./include}/SFNetworking.h"] ["-include ${header}/Networking.h"] (builtins.head (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${e.cc} -dynamiclib") old.buildPhase));
 installPhase=''
  mkdir -p $out/lib $out/include
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-ar rcs $out/lib/libFinchNetworking.a objects/*.o
  cp ${header}/Networking.h $out/include/
 '';
})
