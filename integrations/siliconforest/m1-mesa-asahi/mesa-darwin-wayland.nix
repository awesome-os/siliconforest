# The Darwin Mesa with the Wayland EGL platform added (client side), for GPU-rendered Wayland
# applications.  Same source and options as mesa-darwin.nix plus -Dplatforms=wayland and
# patches 0006/0007.  Recipe: mesa-darwin-build.nix.
import ./mesa-darwin-build.nix { wayland = true; }
