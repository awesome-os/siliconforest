# SPDX-License-Identifier: MIT
import ../tests/conformance.nix {
  name = "StoreKit";
  arc = true;
  foundation = import ../../terminal-app/build-foundation.nix;
  programs = [];
  probe = ./probe.m;
}
