# SPDX-License-Identifier: MIT
import ../tests/conformance.nix {
  name = "CryptoTokenKit";
  programs = [ "cryptotokenkit-test" "cryptotokenkit-empty-test" ];
}
