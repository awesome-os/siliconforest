// SPDX-License-Identifier: MIT
// Built as plain arm64, then given the arm64e subtype in its header (../build-macos-apps-root.nix):
// a dyld that takes the pointer-authentication keys as enabled refuses to load the (arm64)
// libSystem into it; the dyld of ../../libsystem-default/patches/dyld-arm64e-keys-off.patch
// measures that signing does nothing and runs it. The installer's proof for /usr/lib/dyld.
#include <stdio.h>
int main(void) { printf("sf-arm64e-probe: an arm64e program runs with arm64 libraries\n"); return 0; }
