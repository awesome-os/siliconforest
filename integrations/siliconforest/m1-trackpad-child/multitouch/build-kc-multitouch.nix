# The all-drivers collection (../../m1-dcp/build-kc.nix, by default with SFAgx and Wi-Fi, on the
# patched kernel) with one change: the SFM1SPIHID bundle is the multitouch build of this
# directory instead of ../../m1-trackpad-input-trace's. Nothing else in the recipe is touched.
{ agx ? true, wifi ? true, slim ? true }:
# Since 12 November 2026 the default source is upstream's tip (../../rebase/pd.nix), where this is
# ../../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../../rebase/pd.nix).tip then import ../../rebase/build-kc.nix { } else
let
 base = import ../../m1-dcp/build-kc.nix { inherit agx wifi slim; };
 old = import ../../m1-trackpad-input-trace/build-input-trace.nix;
 next = import ./build-multitouch.nix;
 oldBundle = "${old}/System/Library/Extensions/SFM1SPIHID.kext";
 replacement = "${next}/System/Library/Extensions/SFM1SPIHID.kext";
in base.overrideAttrs (prev:
 # exactly one SFM1SPIHID bundle on the link line, or refuse
 assert builtins.stringLength prev.buildPhase
   - builtins.stringLength (builtins.replaceStrings [ oldBundle ] [ "" ] prev.buildPhase)
   == builtins.stringLength oldBundle;
 { buildPhase = builtins.replaceStrings [ oldBundle ] [ replacement ] prev.buildPhase; })
