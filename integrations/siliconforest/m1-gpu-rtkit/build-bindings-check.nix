# What the Rust side sees of this port. Nothing in ../m1-rust-kext is edited: its recipes
# are imported and re-run with one more include directory (./src/include, which holds
# linux/soc/apple/{rtkit,mailbox}.h and the empty linux/mailbox_client.h).
#
#   bindingsGen  lkpi's bindgen step with -I./src/include: Linux's bindings_helper.h lines
#                `#include <linux/soc/apple/mailbox.h>` and `<linux/soc/apple/rtkit.h>` then
#                survive the header reduction (status "ok" instead of "absent")
#   check        $out/names: every `bindings::` item rust/kernel/soc/apple/{rtkit,mailbox}.rs
#                use, found or MISSING in bindings_generated.rs (the build fails on MISSING);
#                $out/structs.rs: the generated struct definitions the wrappers bind by layout
#   matrixAsIs   lkpi's module matrix (tools/matrix.py) for the two targets soc::apple::rtkit
#                and soc::apple::mailbox with these bindings. mailbox.rs has no error of its
#                own left. rtkit.rs has exactly one, which this recipe expects and records:
#                rtkit.rs:192 `bfr_mut.iova = iova as u64` against a `dma_addr_t` that the
#                layer defines as `unsigned long` (vm_paddr_t), which bindgen gives Rust as
#                usize where Linux's is u64. Same size and layout, different Rust type.
#   matrix       the same with `pub type dma_addr_t = u64;` in the generated bindings (one
#                sed on bindgen's output, standing in for a bindgen parameter or a layer
#                typedef that the Rust kext work has to choose): no error of their own in
#                either module, or the build fails. The errors left are those of the
#                modules they pull in (device, devres, of, io ...).
#
#   nix-build build-bindings-check.nix -A check     (or -A matrix, -A matrixAsIs)
let
  pins = import ../m1-rust-kext/pins.nix;
  inherit (pins) pkgs;
  lib = pkgs.lib;
  lkpi = import ../m1-rust-kext/lkpi;
  inherit (lkpi) rust modules;

  needle = "-I$PWD/lnk";
  bindingsGenWith = dmaU64: lkpi.bindingsGen.overrideAttrs (old:
    # refuse a recipe whose include arguments moved (once for the C compiler, once for bindgen)
    assert builtins.length (lib.splitString needle old.buildPhase) == 3;
    {
      name = "sf-lkpi-rust-bindings-with-rtkit" + lib.optionalString dmaU64 "-dma-u64";
      buildPhase = builtins.replaceStrings [ needle ] [ "${needle} -I${./src/include}" ] old.buildPhase;
      postBuild = lib.optionalString dmaU64 ''
        f=$out/objtree/rust/bindings/bindings_generated.rs
        grep -q '^pub type dma_addr_t = vm_paddr_t;$' $f
        sed -i 's/^pub type dma_addr_t = vm_paddr_t;$/pub type dma_addr_t = u64;/' $f
      '';
    });
  bindingsGen = bindingsGenWith false;

  linuxRust = lkpi.linuxRust;
  check = pkgs.runCommand "sf-rtkit-bindings-check" { } ''
    mkdir -p $out
    B=${bindingsGen}/objtree/rust/bindings/bindings_generated.rs
    grep -e 'soc/apple' ${bindingsGen}/status/bindings-headers.tsv > $out/headers.tsv
    cat $out/headers.tsv
    missing=0
    for n in $(cat ${linuxRust}/kernel/soc/apple/rtkit.rs ${linuxRust}/kernel/soc/apple/mailbox.rs \
               | grep -o 'bindings::[A-Za-z_][A-Za-z0-9_]*' | sed 's/bindings:://' | sort -u); do
      if grep -qE "pub (fn|struct|const|type|union) $n\b" $B; then
        echo "found    $n" >> $out/names
      else
        echo "MISSING  $n" >> $out/names
        missing=1
      fi
    done
    cat $out/names
    for s in apple_rtkit_shmem apple_rtkit_ops apple_mbox_msg apple_mbox; do
      sed -n "/^pub struct $s {/,/^}/p" $B >> $out/structs.rs
    done
    grep -c . $out/structs.rs
    test $missing = 0
  '';

  # as lkpi/default.nix builds them, on the bindings above
  matrixWith = dmaU64: let
  bindingsGen = bindingsGenWith dmaU64;
  bindings = lkpi.linuxCrate {
    crateName = "bindings";
    root = "bindings/lib.rs";
    deps = [ lkpi.ffi lkpi.pin_init ];
    env = { OBJTREE = "${bindingsGen}/objtree"; };
  };
  uapi = lkpi.linuxCrate {
    crateName = "uapi";
    root = "uapi/lib.rs";
    deps = [ lkpi.ffi lkpi.pin_init ];
    env = { OBJTREE = "${bindingsGen}/objtree"; };
  };
  kernelDeps = [ lkpi.ffi lkpi.pin_init lkpi.build_error bindings uapi lkpi.zerocopy ];
  matrixSpec = pkgs.writeText "matrix-targets-rtkit.json" (builtins.toJSON {
    core = modules.core;
    gated = lib.splitString " " (lib.removeSuffix "\n" (builtins.readFile "${lkpi.rustTree}/sf-delta/modules")) ++ [ "hrtimer" ];
    targets = builtins.filter (t: lib.hasPrefix "soc::apple::" t.name) modules.matrixTargets;
  });
  matrix = pkgs.stdenvNoCC.mkDerivation {
    name = "sf-rtkit-kernel-crate-matrix" + lib.optionalString (!dmaU64) "-as-is";
    dontUnpack = true;
    nativeBuildInputs = [ rust.rustc pkgs.python3 ];
    buildPhase = ''
      runHook preBuild
      ${lib.concatStringsSep "\n" (lib.mapAttrsToList (k: v: "export ${k}=${lib.escapeShellArg v}") rust.env)}
      export OBJTREE=${bindingsGen}/objtree
      mkdir -p $out
      python3 ${../m1-rust-kext/lkpi/tools/matrix.py} ${matrixSpec} $out -- \
        rustc ${lib.escapeShellArgs rust.rustFlags} --sysroot ${rust.sysroot} \
          --edition=2021 --crate-type rlib --crate-name kernel --error-format=short \
          -Astable_features -Aunused_features --cap-lints=allow \
          ${lib.concatMapStringsSep " " (c: "--cfg ${lib.escapeShellArg c}") modules.config} \
          ${lib.concatMapStringsSep " " (d: "--extern ${d.passthru.crateName}=${d}/lib/lib${d.passthru.crateName}.rlib -L dependency=${d}/lib") kernelDeps} \
          --extern macros=${lkpi.hostMacros}/lib/libmacros.so --extern zerocopy_derive=${lkpi.hostMacros}/lib/libzerocopy_derive.so \
          -L dependency=${lkpi.hostMacros}/lib \
          ${lkpi.rustTree}/kernel/lib.rs | tee $out/matrix.txt
      for t in rtkit mailbox; do
        grep "kernel/soc/apple/$t.rs" "$out/errors/soc::apple::$t.txt" | sed 's#^/nix/store/[^/]*/##' >> $out/own-errors || true
      done
      touch $out/own-errors
      echo "errors of the two modules' own:"; cat $out/own-errors
      ${if dmaU64 then ''
        test ! -s $out/own-errors
      '' else ''
        # exactly the one known line
        test "$(wc -l < $out/own-errors)" = 1
        grep -q '^kernel/soc/apple/rtkit.rs:192:.*expected `usize`, found `u64`' $out/own-errors
      ''}
      runHook postBuild
    '';
    dontInstall = true;
    dontFixup = true;
  };
  in matrix;
  matrix = matrixWith true;
  matrixAsIs = matrixWith false;
in
{ inherit bindingsGen check matrix matrixAsIs; }
