# Root-owned offline KC: retained native50 plus one read-only port observer.
# No bringup/linkup writer bundle, P_PCIE_INIT or shared-write backend is used.
let
  base = import ../../m1-trackpad-input-trace/build-kc.nix;
  observer = /nix/store/qri5ibmds8ha8a0vc38yhnx4kir4n4fa-puredarwin-apcie-bridge-observe-0.1/System/Library/Extensions/PDApcieT8103Bridge.kext;
  needle = "  -o kernel";
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    - builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase)
    == builtins.stringLength needle;
  {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${observer}" \
      -o kernel'' ] old.buildPhase;
  })
