let
  pd = (import ../rebase/pd.nix).pd;
in pd.packages.x86_64-linux.libsystem-arm64.overrideAttrs (old: {
  pname = "puredarwin-libsystem-pagesize";
  patches = (old.patches or []) ++ [ ./fix-root/vm-page-size-symbol.patch ];
})
