# SPDX-License-Identifier: MIT
# A collection recipe's buildPhase with the pinned kernel replaced by ./kernel.nix.
# The base recipes name the kernel as a path literal, which nix copies to the store under
# the name below; the replacement fails the evaluation when that name is not there.
buildPhase:
let
  kernel = import ./kernel.nix { };
  pinned = "/nix/store/3n6shh4izxv6p9gz1cb99kwbzsivpdd1-1ys1x1hzkq3v0l8bc14v2bq1jm5i46zn-puredarwin-kernel-arm64-t8103-bringup-0.1";
  swapped = builtins.replaceStrings [ pinned ] [ "${kernel}" ] buildPhase;
in
assert swapped != buildPhase;
swapped
