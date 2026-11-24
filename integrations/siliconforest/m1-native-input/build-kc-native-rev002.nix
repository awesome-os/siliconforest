# Same collection producer and inputs as the host regression, but replace the
# serial event producer with the already qualified native SPI keyboard bundle.
# The pinned recipe names SFSerialHID explicitly; refuse a recipe drift rather
# than silently retaining a second keyboard producer.
let
  base = import ./build-kc-host-regression-rev002.nix;
  serialArgument = ''      -kext "$KEXTS/SFSerialHID.kext" \
'';
  nativeBundle = /nix/store/z86ncnnnlnyx9dhyn4ab8qc57irz80ra-puredarwin-m1-spi-keyboard-0.1/System/Library/Extensions/SFM1SPIHID.kext;
  nativeArgument = ''      -kext "${nativeBundle}" \
'';
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    - builtins.stringLength (builtins.replaceStrings [ serialArgument ] [ "" ] old.buildPhase)
    == builtins.stringLength serialArgument;
  {
    buildPhase = builtins.replaceStrings [ serialArgument ] [ nativeArgument ] old.buildPhase;
  })
