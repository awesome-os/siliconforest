# SPDX-License-Identifier: MIT
import ../objc-framework.nix {
  name = "UserNotifications";
  patches = [ ./patches/0001-cf-url-type-key.patch ];
}
