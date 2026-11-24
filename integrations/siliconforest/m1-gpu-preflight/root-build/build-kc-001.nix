# Root-owned offline collection: exact qualified native50 + one PMGR observer.
let
  base = import ../../m1-trackpad-input-trace/build-kc.nix;
  observer = /nix/store/idhfac79vrc25630h148745gz1f122zf-puredarwin-gpu-preflight-0.1/System/Library/Extensions/PDGPUPreflight.kext;
  needle = "  -o kernel";
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    - builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase)
    == builtins.stringLength needle;
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${observer}" \
      -o kernel'' ] old.buildPhase;
  })
