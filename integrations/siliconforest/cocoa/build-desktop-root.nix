# SPDX-License-Identifier: MIT
# The Cocoa system root for the SiliconForest desktop: ./build-cocoa-root.nix with our patched
# WindowServer, AppKit, Onyx2D, CoreText and CoreGraphics (./patched.nix: compositor-drawn frames, display scale,
# glyphs at the device scale, and what Apple's Terminal.app needed: patches 0019 to 0027) laid over
# upstream's, and the test application.
let
  p = import ./patched.nix;
in import ./build-cocoa-root.nix {
  # upstream's own desktop session job belongs to its compositor, not ours
  remove = [ "System/Library/LaunchDaemons/org.siliconforest.wayland-session.plist" ];
  extra = [ p.onyx2d p.windowserver p.appkit p.coretext p.coregraphics (import ./build-hello.nix) (import ./build-popups.nix) ];
}
