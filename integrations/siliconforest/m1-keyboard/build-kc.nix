# Same reviewed native003/display collection, plus the actual SDK-built bundle.
# extraKexts is the pinned kc-arm64.nix interface, not a handbook attribute.
let base = import ../chonkstep-darwin/build-kc.nix;
in base.override {
  extraKexts = [ /nix/store/z86ncnnnlnyx9dhyn4ab8qc57irz80ra-puredarwin-m1-spi-keyboard-0.1/System/Library/Extensions/PDM1SPIHID.kext ];
}
