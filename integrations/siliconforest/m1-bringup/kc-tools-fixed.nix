# Same locked kc-tools input and dependencies, with one ARM64 layout fix.
let
  pd = (import ../rebase/pd.nix).pd;
in
pd.inputs.kc-tools.packages.x86_64-linux.default.overrideAttrs (old: {
  pname = "kc-tools-arm64-text-fixed";
  patches = (old.patches or [ ]) ++ [ ./kc-tools-arm64-text.patch ];
})
