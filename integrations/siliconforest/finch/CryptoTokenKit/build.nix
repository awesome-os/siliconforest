# SPDX-License-Identifier: MIT
import ../objc-framework.nix {
  name = "CryptoTokenKit";
  arc = false;
  security = import ../../homebrew/build-security.nix;
}
