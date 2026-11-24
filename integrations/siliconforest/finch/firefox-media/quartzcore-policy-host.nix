# SPDX-License-Identifier: MIT
# Host-only fake-callback execution of the exact patched frozen upload bodies.
{originalJSON}:
let
 e=import ../environment.nix;
 raw=builtins.head (builtins.attrValues (builtins.fromJSON (builtins.readFile originalJSON)).derivations);
 src=builtins.storePath raw.env.src;
in e.pkgs.runCommand "siliconforest-quartzcore-nv12-caller-policy-host" {
 nativeBuildInputs=[e.pkgs.stdenv.cc e.pkgs.python3 e.pkgs.patch];
} ''
 cp ${src}/CARenderer.m CARenderer.m;chmod u+w CARenderer.m
 patch --fuzz=0 -p1 < ${./quartzcore-nv12-copy.patch}
 python3 - <<'PY'
 from pathlib import Path
 source=Path('CARenderer.m').read_text()
 start=source.index('#define SFCA_GL_R8 ')
 end=source.index('// luma from unit 0, CbCr from unit 1',start)
 Path('quartzcore-upload-body.h').write_text(source[start:end])
 PY
 cp ${./quartzcore-nv12-copy.h} quartzcore-nv12-copy.h
 cc -std=gnu11 -O1 -g -Werror=implicit-function-declaration -fsanitize=address,undefined \
  -fno-omit-frame-pointer -pthread -I. -I${e.pkgs.libglvnd.dev}/include \
  ${./host-test/quartzcore-nv12-copy.c} -o policy
 mkdir -p $out;./policy > $out/result.txt
''
