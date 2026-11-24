# SPDX-License-Identifier: MIT
# ./build-kc.nix (the native-boot collection) with the kernel of ./kernel.nix in place of
# ../m1-boot-speed/kernel.nix; every kext is the same build. For every boot that could, by
# a mistake in what is handed to the kernel, come up without its command line.
#   nix-build build-kc-norecovery.nix --no-out-link
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; platform = import ./build-platform.nix; } else
let
  base = import ./build-kc.nix { inherit kernelConfig; };
  from = import ../m1-boot-speed/kernel.nix { inherit kernelConfig; };
  to = import ./kernel.nix { inherit kernelConfig; };
in base.overrideAttrs (old:
  let phase = builtins.replaceStrings [ "${from}" ] [ "${to}" ] old.buildPhase;
  in assert phase != old.buildPhase; { buildPhase = phase; })
