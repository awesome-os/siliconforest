# M1 kernel collection built from the kexts that build-from-patch.nix produces in-tree.
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix): the default
# collection (../rebase/build-kc.nix) with its PCIe bridge and Wi-Fi pair taken from the patch
# build. What follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then (let k = import ./build-from-patch.nix; in import ../rebase/build-kc.nix { bridge = k; wifi = k; }) else
let
  base = import ../m1-trackpad-input-trace/build-kc.nix;
  k = import ./build-from-patch.nix;
  needle = "  -o kernel";
in base.overrideAttrs (old: {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${k}/System/Library/Extensions/SFApcieT8103Bridge.kext" \
      -kext "${k}/System/Library/Extensions/IO80211Family.kext" \
      -kext "${k}/System/Library/Extensions/SFBwfm.kext" \
      -o kernel'' ] old.buildPhase;
  })
