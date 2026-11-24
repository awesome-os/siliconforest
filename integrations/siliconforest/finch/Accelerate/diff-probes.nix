# SPDX-License-Identifier: MIT
let e=import ../environment.nix; sparse=import ./build.nix {groups=["SparseBLAS" "Sparse"];}; in
e.pkgs.runCommand "siliconforest-sparse-difference-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -isysroot ${e.sdk} -O2 -fblocks -DSF_COMPONENT_Sparse -I${sparse}/headers -I${e.libsystem}/usr/include \
  ${./preconditioner-probe.c} --ld-path=${e.ld} -nostdlib -L${sparse}/staged-bootstrap -L${e.libsystem}/usr/lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 \
  -lSparse -lSparseBLAS -lSystem -o $out/bin/preconditioner-probe
''
