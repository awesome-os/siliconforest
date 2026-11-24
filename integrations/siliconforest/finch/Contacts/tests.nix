# SPDX-License-Identifier: MIT
import ../tests/conformance.nix {
  name = "Contacts";
  programs = [ "contacts-test" ];
  arc = true;
}
