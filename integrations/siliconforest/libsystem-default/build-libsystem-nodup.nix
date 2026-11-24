# SPDX-License-Identifier: MIT
# Candidate for the lab's default arm64 libSystem (./build-libsystem.nix) in which no symbol
# is defined twice (GitHub issue 46, follow-up).
#
# The default's link prints 58 `ld: warning: duplicate symbol` lines for libSystem.B.dylib:
# a hand-written copy in stub/sf_libSystem_compat.c, libsystem_kernel's wrappers/_libc_funcptr.c
# or dyld/sf_libc_common_storage.c next to the real routine in libc, libplatform, libpthread
# or libsystem_kernel. ld64 keeps the definition it loads last and says nothing more. Read
# back from the built library's debug map: in all 58 pairs the real routine is the one in
# the image. patches/no-duplicate-definitions.patch makes that the rule instead of an effect
# of link order: 57 of the copies become weak definitions (a weak definition gives way to a
# strong one without a warning and stays the fallback where nothing else is linked).
#
# Not changed, and allowed by the check below:
#   _memchr           the dispatcher in _libc_funcptr.c is exported; weak, it would be a weak
#                     export of libdyld.dylib (which has only that copy) and join dyld's weak
#                     coalescing. libplatform's memchr wins in libSystem.B.dylib, as before.
#   dyld's own pairs  (___stdoutp, ___stderrp, ___cxa_atexit, _memset from upstream's glue.c;
#                     _environ, _arc4random*, _voucher_mach_msg_* from sf_stubs.c;
#                     __NSGetMachExecuteHeader; _mach_task_self): the dyld executable's link,
#                     which the lab does not stage. Listed, not resolved.
# Any other duplicate definition fails the build.
#
#   nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-libsystem-nodup.nix
(import ./build-libsystem.nix).overrideAttrs (old: {
  pname = "siliconforest-libsystem-default-nodup";
  patches = old.patches ++ [ ./patches/no-duplicate-definitions.patch ];
  preBuild = (old.preBuild or "") + ''
    sf_ninja() {
      ( set -o pipefail; ninja "$@" 2>&1 | tee "$NIX_BUILD_TOP/sf-ninja.log" )
    }
  '';
  buildPhase = builtins.replaceStrings [ "ninja -C build-nix" ] [ "sf_ninja -C build-nix" ] old.buildPhase;
  postBuild = (old.postBuild or "") + ''
    awk '
      /ld: warning: duplicate symbol/ {
        sym = $0; sub(/.*duplicate symbol ./, "", sym); sub(/. in:.*/, "", sym)
        getline a; getline b
        dyld = (a ~ /\/dyld\/CMakeFiles\/dyld\.dir\// || b ~ /\/dyld\/CMakeFiles\/dyld\.dir\//)
        if (dyld || sym == "_mach_task_self" || sym == "_memchr") { allowed++; print "duplicate (allowed): " sym; next }
        bad++; print "duplicate definition: " sym; print a; print b
      }
      END { printf "duplicate definitions: %d allowed, %d not allowed\n", allowed, bad; exit bad ? 1 : 0 }
    ' "$NIX_BUILD_TOP/sf-ninja.log"
  '';
})
