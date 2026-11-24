# The m1-apcie-host kernel collection (the SFApcieT8103Bridge build named in
# ../m1-apcie-host/build-kc.nix) plus IO80211Family and SFBwfm.
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { } else
let
  base = import ../m1-apcie-host/build-kc.nix;
  wifi = import ./build-kexts.nix { };
  needle = "  -o kernel";
in base.overrideAttrs (old:
  # refuse a base recipe whose link line moved, rather than silently building it unchanged
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${wifi}/System/Library/Extensions/IO80211Family.kext" \
      -kext "${wifi}/System/Library/Extensions/SFBwfm.kext" \
      -o kernel'' ] old.buildPhase;
  })
