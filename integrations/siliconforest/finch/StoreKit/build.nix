# SPDX-License-Identifier: MIT
import ../objc-framework.nix {
  name = "StoreKit"; version = "379";
  foundation = import ../../terminal-app/build-foundation.nix;
}
