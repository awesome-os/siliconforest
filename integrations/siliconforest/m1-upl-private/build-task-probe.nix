# SPDX-License-Identifier: MIT
# Same pinned native toolchain/UAPI as pin-only; no target action while building.
let
  base = import ./build-pin-probe.nix;
  inherit (import ../rebase/pd.nix) pkgs;
in base.overrideAttrs (old:
  assert builtins.length (pkgs.lib.splitString (builtins.unsafeDiscardStringContext "${./pin-probe.c}") old.buildCommand) == 2;
{
  name = "siliconforest-gpu-bo-task-death-probe";
  buildCommand = builtins.replaceStrings [ "${./pin-probe.c}" "pin-probe" ] [ "${./task-probe.c}" "task-probe" ] old.buildCommand;
})
