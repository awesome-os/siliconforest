# The bridge + Wi-Fi collection (../m1-bwfm/build-kc.nix) plus m1-gpu-dt's SFGpuDT.kext and
# SFRustPlat.kext. Boot with `sfgpudt=2 sfgpudt_stringprops=1`: SFGpuDT powers GFX and
# publishes gpu@206400000, SFRustPlat binds to it.
#
# SFRustHello and SFRustLKPI are left out on purpose: kc-builder places the kexts in the
# 16 MiB between the collection header (0xfffffe0006004000) and the kernel's __TEXT
# (0xfffffe0007004000), and with those two the payload is 0x1080000 bytes ("runs over kernel
# __TEXT; lower -kc-base (and ARM64_KC_BASE)"). Each kext that carries the layer is ~2 MiB.
let
  base = import ../m1-bwfm/build-kc.nix;
  plat = import ./build-plat-kext.nix;
  gpuDt = import ../m1-gpu-dt/build-kext.nix;
  needle = "  -o kernel";
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${gpuDt}/System/Library/Extensions/SFGpuDT.kext" \
      -kext "${plat}/System/Library/Extensions/SFRustPlat.kext" \
      -o kernel'' ] old.buildPhase;
  })
