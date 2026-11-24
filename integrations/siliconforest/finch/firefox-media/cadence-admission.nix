# SPDX-License-Identifier: MIT
# Same public admission program with the diagnostic dylib needed at load time.
let base=import ./codec-admission.nix;trace=import ./cadence-trace.nix;in
base.overrideAttrs(old:{
 name="siliconforest-firefox-cadence-admission-arm64";
 buildCommand=(builtins.replaceStrings ["-o $out/bin/capture-probe"]
 ["-Wl,-needed_library,${trace}/lib/SFMediaCadenceTrace.dylib -Wl,-rpath,@executable_path/../lib -o $out/bin/capture-probe"] old.buildCommand)+''
  mkdir -p $out/lib
  cp ${trace}/lib/SFMediaCadenceTrace.dylib $out/lib/
 '';
})
