# (a) The mailbox/RTKit port compiled for arm64 against the LinuxKPI layer with exactly the
# flag set the Rust kext's C is compiled with (../m1-rust-kext/lkpi/cflags.nix `cc`, the
# list bindgen and the rust/helpers objects use), plus this directory's two include
# directories. Output:
#   $out/lib/libsfrtkit.a          mailbox.o rtkit.o rtkit-crashlog.o sf_rtkit_compat.o
#   $out/nix-support/commands      the compile lines
#   $out/nix-support/defined       functions the archive defines
#   $out/nix-support/undefined     what it needs from the layer and from sf_rtkit_platform.h
#   $out/nix-support/api-check     every apple_rtkit_*/apple_mbox_* function declared in the
#                                  two Linux headers is defined (the build fails otherwise)
# -O2 is this recipe's choice, as for the helpers: the tree's kext C gets no -O flag.
# The C++ side (src/SFRtkitPlatform.cpp) needs xnu's headers and is compiled by
# build-probe-kext.nix, which also compiles these four files again through cmake.
let
  pins = import ../m1-rust-kext/pins.nix;
  inherit (pins) pkgs crossToolchain;
  lib = pkgs.lib;
  lkpi = import ../m1-rust-kext/lkpi;
  cflags = lkpi.cflags;
  clang = "${crossToolchain}/bin/arm64-apple-darwin20.4-clang";
  src = ./src;
  files = [ "mailbox.c" "rtkit.c" "rtkit-crashlog.c" "sf_rtkit_compat.c" ];
in
pkgs.stdenv.mkDerivation {
  name = "sf-rtkit-lkpi-lib";
  dontUnpack = true;
  buildPhase = ''
    runHook preBuild
    export DARWIN_SDK_ROOT=${cflags.sysroot}
    mkdir -p obj $out/lib $out/nix-support
    CC=(${clang} ${lib.escapeShellArgs cflags.cc} -I${src}/include -I${src} -O2)
    for f in ${lib.concatStringsSep " " files}; do
      echo "''${CC[*]} -c ${src}/$f -o obj/''${f%.c}.o" >> $out/nix-support/commands
      "''${CC[@]}" -c ${src}/$f -o obj/''${f%.c}.o
    done
    ${crossToolchain}/bin/arm64-apple-darwin20.4-ar rcsD $out/lib/libsfrtkit.a obj/mailbox.o obj/rtkit.o obj/rtkit-crashlog.o obj/sf_rtkit_compat.o
    NM=${crossToolchain}/bin/arm64-apple-darwin20.4-nm
    $NM obj/*.o | grep ' [TtSsDdBb] _' | sed 's/.* _//' | grep -v '^ltmp' | sort -u > $out/nix-support/defined
    $NM -u obj/*.o | sed 's/^ *//' | grep '^_' | sed 's/^_//' | sort -u > all-undefined
    # undefined after linking the four objects together
    comm -23 all-undefined $out/nix-support/defined > $out/nix-support/undefined

    # every function the two Linux headers declare must be defined
    : > $out/nix-support/api-check
    missing=0
    for fn in $(cat ${src}/include/linux/soc/apple/rtkit.h ${src}/include/linux/soc/apple/mailbox.h \
                | tr '\n' ' ' | grep -o '\(devm_\)\?apple_\(rtkit\|mbox\)_[a-z0-9_]*(' | tr -d '(' | sort -u); do
      if grep -qx "$fn" $out/nix-support/defined; then
        echo "defined  $fn" >> $out/nix-support/api-check
      else
        echo "MISSING  $fn" >> $out/nix-support/api-check
        missing=1
      fi
    done
    cat $out/nix-support/api-check
    test $missing = 0
    runHook postBuild
  '';
  dontInstall = true;
  dontFixup = true;
}
