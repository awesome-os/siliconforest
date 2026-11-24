# SPDX-License-Identifier: MIT
# Staged composition only. TERM owns the compositor-clock qualification;
# FX owns selection of ONE coherent surface/CGL/CV/VT browser family.
import ../VideoToolbox/zerocopy/corevideo-with-base.nix {
 provider=import ../../cocoa/corevideo-displaylink.nix;
}
