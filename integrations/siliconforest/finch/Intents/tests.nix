# SPDX-License-Identifier: MIT
import ../tests/conformance.nix {
  name = "Intents";
  arc = false;
  foundation = import ../../terminal-app/build-foundation.nix;
  programs = [ "intents-test" ];
}
