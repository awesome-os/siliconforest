# SPDX-License-Identifier: MIT
# The desktop root (./build-desktop-root.nix: our patched WindowServer, AppKit and Onyx2D) plus
# what the official macOS Firefox links beyond it: upstream's small application frameworks
# built for arm64 with our AuthenticationServices patch (./patched.nix), Security and
# SystemConfiguration (upstream's arm64 builds), and our own Metal, MetalKit and Accelerate
# (./build-own-frameworks.nix).
let
  a = import ./arm64.nix;
  p = import ./patched.nix;
in import ./build-cocoa-root.nix {
  # SystemConfiguration's configd links a private framework (SymptomReporter) that has no arm64
  # build at the pin; launchd would restart it for ever. The framework itself answers without it.
  # (and upstream's own desktop session job, which belongs to its compositor, not ours)
  remove = [ "System/Library/LaunchDaemons/com.apple.configd.plist" "System/Library/LaunchDaemons/org.siliconforest.wayland-session.plist" ];
  extra = [
    p.appFrameworks
    p.security
    a.pd."systemConfiguration-arm64"
    (import ./build-own-frameworks.nix)
    p.onyx2d p.windowserver p.appkit p.coretext p.coregraphics
    (import ./build-hello.nix)
  ];
}
