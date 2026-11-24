# SPDX-License-Identifier: MIT
# Unmodified public VT decoder, baseline versus the needed diagnostic image.
let
 e=import ../environment.nix;
 original=import ../VideoToolbox/zerocopy/capture-probe.nix;
 trace=import ./cadence-trace.nix;
 patchLine="${e.pkgs.python3}/bin/python3 ${../VideoToolbox/zerocopy/capture-probe.py} capture-probe.c";
 base=original.overrideAttrs(old:{
  name="siliconforest-cadence-vt-baseline-arm64";
  buildCommand=assert builtins.replaceStrings [patchLine] [""] old.buildCommand != old.buildCommand;
    builtins.replaceStrings [patchLine] ["true"] old.buildCommand;
 });
 observed=base.overrideAttrs(old:{
  name="siliconforest-cadence-vt-observed-arm64";
  buildCommand=(builtins.replaceStrings ["-o $out/bin/capture-probe"]
   ["-Wl,-needed_library,${trace}/lib/SFMediaCadenceTrace.dylib -Wl,-rpath,@executable_path/../lib -o $out/bin/capture-probe"] old.buildCommand);
 });
in e.pkgs.runCommand "siliconforest-cadence-vt-probes-arm64" {} ''
 mkdir -p $out/bin $out/lib
 cp ${base}/bin/capture-probe $out/bin/vt-baseline
 cp ${observed}/bin/capture-probe $out/bin/vt-observed
 cp ${trace}/lib/SFMediaCadenceTrace.dylib $out/lib/
''
