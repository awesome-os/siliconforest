# The phase A kernel collection of the Rust kext work (../m1-rust-kext/build-kc.nix: bridge,
# Wi-Fi kexts, SFRustHello) plus SFRtkitProbe.kext, composed like
# ../m1-rust-kext/build-lkpi-kc.nix. kc-builder reports unresolved symbols as
# `UNRESOLVED: <sym>` lines without failing: read the build log (`nix log <this drv>`).
# dyld_stub_binder is unresolved for every kext.
let
  base = import ../m1-rust-kext/build-kc.nix;
  probe = import ./build-probe-kext.nix;
  needle = "  -o kernel";
in base.overrideAttrs (old:
  # refuse a base recipe whose link line moved, rather than silently building it unchanged
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${probe}/System/Library/Extensions/SFRtkitProbe.kext" \
      -o kernel'' ] old.buildPhase;
  })
