# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  cross = import ./toolchain.nix;
in e.pkgs.runCommand "siliconforest-finch-network-foundation-prerequisites" {} ''
  ${cross}/swiftc -module-cache-path $TMPDIR/modules -typecheck ${./foundation-probe.swift}
  mkdir -p $out
  touch $out/typecheck-passed
''
