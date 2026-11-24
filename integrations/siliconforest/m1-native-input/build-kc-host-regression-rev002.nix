# No native SPI member or hardware opt-in: exercise the changed family through
# the already qualified serial-HID terminal workload before native integration.
let base = import ../m1-display/console-return/build-kc.nix;
in base.override {
  genericKexts = import ./build-generic-selection-rev002.nix;
  extraKexts = [];
}
