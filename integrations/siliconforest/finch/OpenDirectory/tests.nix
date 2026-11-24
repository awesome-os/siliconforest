# SPDX-License-Identifier: MIT
let
  fw = import ./build.nix;
  prefix = "/System/Library/Frameworks/OpenDirectory.framework/Versions/A";
  cf = "${prefix}/Frameworks/CFOpenDirectory.framework/Versions/A/CFOpenDirectory";
in import ../tests/conformance.nix {
  name = "OpenDirectory";
  programs = [ "opendirectory-objects-test" ];
  probe = ./probe.m;
  extraLinks = "-Wl,-dylib_file,${cf}:${fw}${cf} -Wl,-dylib_file,${prefix}/Libraries/libfinchaccount.dylib:${fw}${prefix}/Libraries/libfinchaccount.dylib";
}
