# SPDX-License-Identifier: MIT
# build-kc-rw.nix for boots without the hypervisor (the platform expert that knows held cores).
{ kernelConfig ? "development" }:
import ./build-kc-rw.nix { inherit kernelConfig; native = true; }
