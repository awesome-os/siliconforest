# SPDX-License-Identifier: MIT
# libSystem and dyld for macOS applications built for arm64e against the macOS 26 SDK
# (GitHub issue 52, docs/roadmap/issues/SF-Y-terminal-app.md): since the replacement on the M1
# this is the lab's default libSystem recipe itself, whose patch list has
#   ../libsystem-default/patches/dyld-arm64e-keys-off.patch
#   ../libsystem-default/patches/dyld-objc-writes-data-const.patch
#   ../libsystem-default/patches/macos26-sdk-imports.patch
# (the recipe says what each does). Kept as a name the recipes of this directory import.
#
# Since 15 November 2026 (rehearsals pf2b/pf2b-2, installed on the M1's disk): the same source and
# patches compiled with optimisation, ../libsystem-default/build-libsystem-opt.nix (libSystem a715a1e2,
# dyld be18e963; SF-Y, "Performance: optimised core libraries"). SF_LIBSYSTEM=debug: SiliconForest's -O0
# build of before (the default recipe itself, ba8013eb/a0da8333), an environment variable as
# ../rebase/pd.nix's SF_PD. The RAM root and QEMU images still stage the default recipe.
#
#   nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-libsystem.nix
let which = builtins.getEnv "SF_LIBSYSTEM"; in
if which == "" || which == "opt" then import ../libsystem-default/build-libsystem-opt.nix { }
else if which == "debug" then import ../libsystem-default/build-libsystem.nix
else throw "SF_LIBSYSTEM=${which}: expected opt or debug"
