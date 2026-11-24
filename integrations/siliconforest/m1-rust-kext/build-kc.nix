# The m1-bwfm kernel collection (bridge + IO80211Family + SFBwfm, see
# ../m1-bwfm/build-kc.nix) plus SFRustHello.kext.
# kc-builder reports unresolved symbols as `UNRESOLVED: <sym>` lines without failing:
# read the build log (`nix log <this drv>`). dyld_stub_binder is unresolved for every kext.
let
  base = import ../m1-bwfm/build-kc.nix;
  rustHello = import ./build-kext.nix;
  needle = "  -o kernel";
in base.overrideAttrs (old:
  # refuse a base recipe whose link line moved, rather than silently building it unchanged
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${rustHello}/System/Library/Extensions/SFRustHello.kext" \
      -o kernel'' ] old.buildPhase;
  })
