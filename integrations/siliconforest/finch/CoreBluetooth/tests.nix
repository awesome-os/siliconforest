# SPDX-License-Identifier: MIT
import ../tests/conformance.nix {
  name = "CoreBluetooth";
  programs = [ "corebluetooth-test" "corebluetooth-unsupported-test" ];
}
