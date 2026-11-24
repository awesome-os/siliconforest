// SPDX-License-Identifier: MIT
// kIOMainPortDefault: the macOS 12+ name of kIOMasterPortDefault (both MACH_PORT_NULL, "use
// the default main port"). Programs built against a current SDK import this name (htop,
// ffmpeg); the tree's IOKit exports only the old one. Linked into libIOKitCF.dylib by
// build-iokit.nix. Upstream fix: one line next to kIOMasterPortDefault in
// src/Libraries/IOKit/IOKitLibCF.c and one in IOKit.exports.
const unsigned int kIOMainPortDefault = 0;
