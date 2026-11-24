# SPDX-License-Identifier: MIT
# Private prefix ONLY. Canonical Foundation remains the core owner's guarded
# installation. Reuse its exact application/networking recipe, add one hook.
let e=import ../../environment.nix; base=import ../../../terminal-app/build-foundation-networking.nix; in
base.overrideAttrs(old:{
 pname="siliconforest-foundation-vlc-nib-private";
 nativeBuildInputs=(old.nativeBuildInputs or [])++[e.pkgs.python3];
 postPatch=(old.postPatch or "")+''
  python3 ${./nib-allocator.py} Coding.subproj/NSKeyedUnarchiver.m
 '';
})
