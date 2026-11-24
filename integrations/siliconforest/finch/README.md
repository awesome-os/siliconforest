# Finch non-GUI ports

Source: Finch `2d9b1bb1af57e14462e4621d194929188a8845b0` (MIT OR Apache-2.0),
read by `source.nix` from the lab's pinned source checkout. Recipes use SiliconForest's
open generated SDK and arm64 cross compiler; arm64e processes can load these
libraries with SiliconForest's existing PAC-disabled dyld.

Build: `nix-build --extra-experimental-features 'nix-command flakes' --no-out-link corecrypto/build.nix`
or `Security/build.nix` from this directory.

Scope, provenance, reconciliation and observed M1 results:
[SF-AB](../../../docs/roadmap/issues/SF-AB-finch-frameworks.md).
