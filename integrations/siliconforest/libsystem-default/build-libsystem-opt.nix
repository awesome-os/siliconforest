# SPDX-License-Identifier: MIT
# The lab's default libSystem (./build-libsystem.nix: the same source and patches) compiled with
# optimisation. SiliconForest's mkSiliconForestBuild configures every libSystem component and dyld with
# CMAKE_BUILD_TYPE=Debug and no -O flag of its own (CMake's Debug flags are "-g"): libmalloc, libc,
# libplatform's string functions (C, not assembly, on arm64), pthread, libdispatch, libxpc, corecrypto
# and dyld all ran -O0 code (each local stored to and loaded from the stack). Here the build type stays
# Debug (so nothing else changes: the same -DDEBUG of corecrypto/CommonCrypto, the same assertions, no
# NDEBUG where SiliconForest did not set it) and its flags become
#     -O2 -g -fno-strict-aliasing
#   -g: symbols and the debug map stay (sf-prof, sf-heap, crash reports); frame pointers stay (arm64
#       Darwin keeps them by default; libsystem_kernel's own -momit-leaf-frame-pointer is unchanged);
#   -fno-strict-aliasing: as for CoreFoundation (../terminal-app/build-corefoundation.nix); the FreeBSD
#       libc, libxpc and SiliconForest's additions pun types freely and were only ever compiled at -O0.
#   + patches/string-functions-no-builtin.patch: -fno-builtin for libplatform and libc's string/
#       sources, the C implementations of memmove/memset/memset_pattern/strlen/...: optimised, their
#       loops could otherwise be compiled into calls to themselves.
#   + patches/platform-memset-wide-stores.patch: memset/bzero and memset_pattern4/8/16 (libplatform's
#       generic C, which arm64 uses) store 8 bytes at a time; they called _platform_memmove() for every
#       4 bytes (memset of 4 KB: about 7 us at -O2, 23 us at -O0). calloc, CF instances and drawing fill.
#   + patches/dyld-libobjc-search-paths.patch (dyld only): SiliconForest's dyld loads /usr/lib/libobjc.A.dylib
#       into every program before the program's own libraries, with DYLD_LIBRARY_PATH switched off for
#       that load, so no libobjc could be tried from a test directory (the guarded installer's step 2).
#       It now follows DYLD_LIBRARY_PATH as every other library does.
# A core file (every program loads libSystem; dyld starts every program): QEMU rehearsal and the guarded
# installer only (docs/roadmap/issues/SF-Y-terminal-app.md, "Installing" and "Performance: optimised
# core libraries"). libSystem.B.dylib and dyld come from this one build; they are installed separately.
#
#   nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-libsystem-opt.nix
#   ... --argstr level -Os      the size-optimised variant (Apple's Release setting for these projects)
{ level ? "-O2" }:
let
  base = import ./build-libsystem.nix;
  flags = "${level} -g -fno-strict-aliasing";
  debug = "-DCMAKE_BUILD_TYPE=Debug";
  set = builtins.concatStringsSep " " (map (l: ''"-DCMAKE_${l}_FLAGS_DEBUG=${flags}"'') [ "C" "CXX" "OBJC" "OBJCXX" ]);
in base.overrideAttrs (old: {
  pname = "siliconforest-libsystem-opt";
  patches = old.patches ++ [ ./patches/string-functions-no-builtin.patch ./patches/platform-memset-wide-stores.patch ./patches/dyld-libobjc-search-paths.patch ];
  configurePhase =
    assert builtins.match ".*${debug}.*" old.configurePhase != null;
    builtins.replaceStrings [ debug ] [ "${debug} ${set}" ] old.configurePhase;
})
