# SPDX-License-Identifier: MIT
import ../tests/conformance.nix {
  name = "CloudKit";
  arc = true;
  probe = ./getter-probe.m;
  programs = [ "cloudkit-unavailable-test" "cloudkit-test" ];
}
