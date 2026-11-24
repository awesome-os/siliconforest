# The current kernel collection (../m1-rust-kext/build-lkpi-kc.nix: bridge, Wi-Fi kexts,
# SFRustHello, SFRustLKPI) plus SFGpuDT.kext. That recipe is imported, not edited.
# kc-builder reports unresolved symbols as `UNRESOLVED: <sym>` lines without failing:
# read the build log (`nix log <this drv>`).
let
  base = import ../m1-rust-kext/build-lkpi-kc.nix;
  gpuDt = import ./build-kext.nix;
  needle = "  -o kernel";
in base.overrideAttrs (old:
  # refuse a base recipe whose link line moved, rather than silently building it unchanged
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${gpuDt}/System/Library/Extensions/SFGpuDT.kext" \
      -o kernel'' ] old.buildPhase;
  })
