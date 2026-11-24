# The phase A kernel collection (./build-kc.nix: bridge, Wi-Fi kexts, SFRustHello) plus
# SFRustLKPI.kext. kc-builder reports unresolved symbols as `UNRESOLVED: <sym>` lines
# without failing: read the build log (`nix log <this drv>`).
let
  base = import ./build-kc.nix;
  rustLkpi = import ./build-lkpi-kext.nix;
  needle = "  -o kernel";
in base.overrideAttrs (old:
  # refuse a base recipe whose link line moved, rather than silently building it unchanged
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${rustLkpi}/System/Library/Extensions/SFRustLKPI.kext" \
      -o kernel'' ] old.buildPhase;
  })
