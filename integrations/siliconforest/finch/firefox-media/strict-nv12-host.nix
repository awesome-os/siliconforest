# SPDX-License-Identifier: MIT
# Execute the literal extension with fake GL/IOSurface under host sanitizers.
{extensionHeader ? ./strict-nv12-plane.h}:
let e=import ../environment.nix;
in e.pkgs.runCommand "siliconforest-strict-nv12-policy-host" {
  nativeBuildInputs=[e.pkgs.stdenv.cc];
} ''
  mkdir host-test
  cp ${./host-test/strict-nv12-plane.c} host-test/strict-nv12-plane.c
  cp ${extensionHeader} strict-nv12-plane.h
  cc -std=gnu11 -O1 -g -Werror=implicit-function-declaration -fsanitize=address,undefined -fno-omit-frame-pointer \
    -pthread -I${e.pkgs.libglvnd.dev}/include host-test/strict-nv12-plane.c -o policy
  mkdir -p $out
  ./policy > $out/result.txt
''
