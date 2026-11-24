# SPDX-License-Identifier: MIT
# Combined native driver build; sfcio_cm=1 is a separate experimental opt-in.
import ./build-kext-base.nix { connectionManager=true; }
