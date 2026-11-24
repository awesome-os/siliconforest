# SPDX-License-Identifier: MIT
# The proposed default desktop collection with sound: ./build-kc.nix (the default desktop
# collection plus SFAppleSound, debug maps stripped) with the quiet-boot kernel of
# ../m1-quiet-boot/kernel.nix in place of ../m1-smp/kernel.nix, exactly as
# ../m1-quiet-boot/build-kc.nix does it for the collection without sound.
#   nix-build build-kc-desktop.nix --argstr kernelConfig development --no-out-link
{ kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; } else
let
  base = import ./build-kc.nix { inherit kernelConfig; };
  from = import ../m1-smp/kernel.nix { inherit kernelConfig; };
  to = import ../m1-quiet-boot/kernel.nix { inherit kernelConfig; };
in base.overrideAttrs (old:
  let phase = builtins.replaceStrings [ "${from}" ] [ "${to}" ] old.buildPhase;
  in assert phase != old.buildPhase; { buildPhase = phase; })
