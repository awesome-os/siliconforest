# SF-F item F11: the Linux kernel API the Asahi driver's G13 path needs beyond what
# ../m1-rust-kext/lkpi and ../m1-drm72 provide (README.md has the per-API decisions).
#
# ../m1-rust-kext/lkpi imports this directory itself (its `extras = ../../m1-lkpi-extras`):
# c/sf_xarray.c goes into its layer tree, helpers/ into its helper overlay, bindgen/ and
# rust/patches into its bindgen step and Rust tree. So "with the additions" is that
# directory's own derivations, used here as they are. This recipe adds what that
# directory does not have: the measurements that show what the additions change.
#
#   nix-build -A clib          (i)  c/*.c compiled for arm64 with the layer's flags
#   nix-build -A matrix        (ii) kernel-crate modules of this ticket on ../m1-rust-kext/lkpi
#   nix-build -A matrixBefore       the same with this directory taken out again
#   nix-build -A report             before/after table (report.md)
#   nix-build -A kernelAgx          the kernel crate on that directory's full driver module
#                                   list (`modules.agx`) plus this ticket's four modules
#   nix-build -A probe              rust/probe built against all of it and linked with the
#                                   helpers and clib; fails on an unresolved helper
#   nix-build -A hostTests          `make test` in the sandbox
#
# Nothing is pinned by store path. Attributes of ../m1-rust-kext/lkpi relied on:
# bindingsGen, rustTree, hostMacros, ffi, build_error, pin_init, zerocopy, bindings, uapi,
# kernelWith, linuxCrate, pm, matrixOnly, cflags (.cc, .sysroot), modules (.core, .plat,
# .agx, .agxConfig), rust (.mkKextObject), and `finalTree` (else `tree`).
#
# `matrixBefore` re-instantiates those derivations with this directory substituted out
# (the mechanism ../m1-drm72 uses, `subst`). It finds what to replace by the names that
# recipe gives its derivations ("sf-lkpi-rust-helpers-overlay", "sf-lkpi-extras-include")
# and by this directory's own name; if one of them is no longer referred to, evaluation
# fails and says which, instead of measuring something else.
let
  pins = import ../m1-rust-kext/pins.nix;
  lkpi = import ../m1-rust-kext/lkpi;
  inherit (pins) pkgs crossToolchain;
  lib = pkgs.lib;
  cflags = lkpi.cflags;
  layerTree = lkpi.finalTree or lkpi.tree;
  clang = "${crossToolchain}/bin/arm64-apple-darwin20.4-clang";
  nm = "${crossToolchain}/bin/arm64-apple-darwin20.4-nm";

  # what the driver needs switched on from this ticket
  extrasModules = [ "xarray" "module_param" "mm" "seq_file" ];

  # ---- substitution (see ../m1-drm72/default.nix, `subst`) ---------------------------------
  substString = pairs: s:
    let
      from = map (p: "${p.from}") pairs;
      to = map (p: "${p.to}") pairs;
      r = builtins.replaceStrings from to s;
      dropped = lib.concatMap (f: builtins.attrNames (builtins.getContext f)) from;
    in builtins.appendContext (builtins.unsafeDiscardStringContext r)
      (builtins.removeAttrs (builtins.getContext r) dropped);
  substAttrs = old: lib.filterAttrs (n: v: builtins.isString v || builtins.isList v)
    (builtins.removeAttrs old [ "name" "pname" "version" "outputs" "nativeBuildInputs" "buildInputs" "passthru" "meta" ]);
  subst = pairs: drv: drv.overrideAttrs (old: lib.mapAttrs (n: v:
      if builtins.isString v then substString pairs v
      else map (e: if builtins.isString e then substString pairs e else e) v)
    (substAttrs old));
  pair = from: to: { inherit from to; };
  # The store path named `name` that a derivation's attributes mention (as text; a path
  # followed by /sub/path counts). Exactly one, or evaluation stops.
  mentioned = what: name: drv:
    let
      strings = lib.concatMap (v: if builtins.isString v then [ v ] else lib.filter builtins.isString v)
        (lib.attrValues (substAttrs drv.drvAttrs));
      words = lib.concatMap (s: lib.filter builtins.isString
        (builtins.split "[ \n\t\"'=();]+" (builtins.unsafeDiscardStringContext s))) strings;
      found = lib.unique (lib.concatMap (w:
        let m = builtins.match "[^/]*(/nix/store/[a-z0-9]+-${name})(/.*)?" w; in
        if m == null then [ ] else [ (builtins.head m) ]) words);
    in
    if builtins.length found == 1 then builtins.head found
    else throw "m1-lkpi-extras: ../m1-rust-kext/lkpi's ${what} refers to ${toString (builtins.length found)} store paths named *-${name} (expected 1); the recipe here must follow that directory";

  # ---- (i) the C added to the layer ---------------------------------------------------------
  cSrc = ./c;
  cFiles = [ "sf_xarray.c" ];
  clib = pkgs.stdenv.mkDerivation {
    name = "sf-lkpi-extras-lib";
    dontUnpack = true;
    buildPhase = ''
      runHook preBuild
      export DARWIN_SDK_ROOT=${cflags.sysroot}
      mkdir -p obj $out/lib $out/nix-support
      # the flag set LinuxKPI itself is compiled with in a kext (../m1-rust-kext/lkpi/cflags.nix);
      # -O2 is this recipe's choice, as for the helpers
      CC=(${clang} ${lib.escapeShellArgs cflags.cc} -I${cSrc}/include -O2)
      for f in ${lib.concatStringsSep " " cFiles}; do
        echo "''${CC[*]} -c ${cSrc}/$f -o obj/''${f%.c}.o" >> $out/nix-support/commands
        "''${CC[@]}" -c ${cSrc}/$f -o obj/''${f%.c}.o
      done
      ${crossToolchain}/bin/arm64-apple-darwin20.4-ar rcsD $out/lib/libsflkpiextras.a $(ls obj/*.o | sort)
      ${nm} obj/*.o | grep ' [TtSsDdBb] _' | sed 's/.* _//' | grep -v '^ltmp' | sort -u > $out/nix-support/defined
      ${nm} -u obj/*.o | sed 's/^ *//' | grep '^_' | sed 's/^_//' | sort -u > all-undefined
      comm -23 all-undefined $out/nix-support/defined > $out/nix-support/undefined
      # every function the header declares is defined
      for fn in $(grep -o 'pd_\{1,3\}xa_[a-z_]*(' ${cSrc}/include/linux/sf_xarray.h | tr -d '(' | sort -u); do
        grep -qx "$fn" $out/nix-support/defined || { echo "declared in sf_xarray.h, not defined: $fn" >&2; exit 1; }
      done
      echo "defined: $(tr '\n' ' ' < $out/nix-support/defined)"
      echo "needs from the layer: $(tr '\n' ' ' < $out/nix-support/undefined)"
      runHook postBuild
    '';
    dontInstall = true;
    dontFixup = true;
  };

  # ---- "before": ../m1-rust-kext/lkpi with this directory taken out --------------------------
  # What that recipe reads from here, emptied: no helpers, no bindgen additions, no Rust
  # patches. (c/sf_xarray.c stays in its layer tree; without the helpers nothing calls it.)
  removed = pkgs.runCommand "m1-lkpi-extras-removed" { } ''
    mkdir -p $out/bindgen $out/helpers $out/rust/patches
    : > $out/bindgen/sf_bindings_extra_add.h
    echo "--raw-line '// m1-lkpi-extras taken out for the before measurement'" > $out/bindgen/bindgen_parameters_add
  '';
  removedInclude = pkgs.runCommand "sf-lkpi-extras-include-removed" { } "mkdir -p $out";
  beforeBg = [
    (pair (mentioned "bindingsGen" "m1-lkpi-extras" lkpi.bindingsGen) removed)
    (pair (mentioned "bindingsGen" "sf-lkpi-rust-helpers-overlay" lkpi.bindingsGen) ../m1-rust-kext/lkpi/helpers)
    (pair (mentioned "bindingsGen" "sf-lkpi-extras-include" lkpi.bindingsGen) removedInclude)
  ];
  before = rec {
    bindingsGen = (subst beforeBg lkpi.bindingsGen).overrideAttrs (old: { name = "sf-lkpi-rust-bindings-without-extras"; });
    rustTree = (subst [
      (pair (mentioned "rustTree" "m1-lkpi-extras" lkpi.rustTree) removed)
      (pair lkpi.bindingsGen bindingsGen)
    ] lkpi.rustTree).overrideAttrs (old: { name = "linux-rust-for-pd-lkpi-without-extras"; });
    base = [ (pair lkpi.bindingsGen bindingsGen) (pair lkpi.rustTree rustTree) (pair lkpi.hostMacros hostMacros) ];
    hostMacros = subst [ (pair lkpi.rustTree rustTree) ] lkpi.hostMacros;
    ffi = subst base lkpi.ffi;
    build_error = subst base lkpi.build_error;
    pin_init = subst base lkpi.pin_init;
    zerocopy = subst base lkpi.zerocopy;
    leaf = base ++ [ (pair lkpi.ffi ffi) (pair lkpi.build_error build_error) (pair lkpi.pin_init pin_init) (pair lkpi.zerocopy zerocopy) ];
    bindings = subst leaf lkpi.bindings;
    uapi = subst leaf lkpi.uapi;
    cratePairs = leaf ++ [ (pair lkpi.bindings bindings) (pair lkpi.uapi uapi) ];
  };

  # ---- (ii) the module matrix ----------------------------------------------------------------
  # Targets of this ticket. `used` is whether the G13 path of the driver reaches the module
  # (README.md has the file:line evidence); it only labels the report.
  top = used: name: { inherit name used; modules = [ name ]; cfgs = [ ]; };
  targets = map (top true) extrasModules ++ [
    { name = "all four together"; used = true; modules = extrasModules; cfgs = [ ]; }
    # used by the G13 path and in this ticket's list, but served by ../m1-rust-kext/lkpi
    # itself (its helpers/scatterlist.c and u64 resource_size_t); here to show its status
    (top true "scatterlist")
  ] ++ map (top false) [
    "maple_tree" "fs" "cred" "debugfs" "rbtree" "revocable" "firmware" "clk" "regulator" "opp"
    "jump_label" "tracepoint" "bitmap" "pid_namespace" "security"
  ] ++ [
    { name = "time::hrtimer"; used = false; modules = [ "hrtimer" ]; cfgs = [ ]; }
    { name = "cpufreq"; used = false; modules = [ "cpufreq" ]; cfgs = [ "CONFIG_CPU_FREQ" ]; }
    { name = "kunit"; used = false; modules = [ "kunit" ]; cfgs = [ ''CONFIG_KUNIT="y"'' ]; }
  ];
  specFor = tree: pkgs.writeText "matrix-targets-extras.json" (builtins.toJSON {
    core = lkpi.modules.core;
    gated = lib.splitString " " (lib.removeSuffix "\n" (builtins.readFile "${tree}/sf-delta/modules")) ++ [ "hrtimer" ];
    inherit targets;
  });
  # that directory's matrix derivation with another target list
  withSpec = spec: drv: drv.overrideAttrs (old:
    let
      words = lib.splitString " " (builtins.unsafeDiscardStringContext old.buildPhase);
      theirSpec = lib.findFirst (w: lib.hasSuffix "-matrix-targets.json" w) null words;
      buildPhase = builtins.replaceStrings [ theirSpec ] [ "${spec}" ] old.buildPhase;
    in
    assert lib.assertMsg (theirSpec != null) "m1-lkpi-extras: matrixOnly no longer passes a *-matrix-targets.json to matrix.py";
    { inherit buildPhase; });
  matrix = (withSpec (specFor lkpi.rustTree) (lkpi.matrixOnly null)).overrideAttrs (old: {
    name = "sf-lkpi-kernel-crate-matrix-extras";
  });
  matrixBefore = (withSpec (specFor before.rustTree) (subst before.cratePairs (lkpi.matrixOnly null))).overrideAttrs (old: {
    name = "sf-lkpi-kernel-crate-matrix-extras-before";
  });
  report = pkgs.runCommand "sf-lkpi-extras-report" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    mkdir -p $out
    python3 ${./tools/report.py} ${specFor lkpi.rustTree} ${matrixBefore}/matrix.json ${matrix}/matrix.json > $out/report.md
    ln -s ${matrixBefore} $out/before
    ln -s ${matrix} $out/after
    cat $out/report.md
  '';

  # ---- the kernel crate as the driver needs it ------------------------------------------------
  # that directory's full module list for the Asahi driver, plus this ticket's four
  kernelAgx = (lkpi.kernelWith (lib.unique (lkpi.modules.agx ++ extrasModules)) lkpi.modules.agxConfig).overrideAttrs (old: {
    name = old.name + "-agx-extras";
  });

  # ---- the probe: everything together, compiled and linked ----------------------------------
  # on the driver-core tier (`modules.plat`), plus this ticket's four
  kernel = lkpi.kernelWith (lib.unique (lkpi.modules.plat ++ extrasModules)) [ ];
  probeLib = lkpi.linuxCrate {
    crateName = "sfextras_probe";
    crateType = "staticlib";
    src = ./rust/probe;
    root = "lib.rs";
    deps = [ kernel lkpi.pin_init ];
    procMacros = [ (lkpi.pm "macros") ];
    # what scripts/Makefile.build allows every kernel module (rust_allowed_features)
    extraFlags = [ "-Zcrate-attr=feature(arbitrary_self_types,derive_coerce_pointee,used_with_arg)" ];
    # module! reads it for the built-in `file` modinfo string
    env = { RUST_MODFILE = "drivers/gpu/drm/asahi/sfextras_probe"; };
  };
  probeObject = lkpi.rust.mkKextObject {
    name = "sfextras_probe";
    staticlib = probeLib;
    exports = [ "_sf_extras_probe" "___sfextras_probe_init" "___sfextras_probe_exit" ];
    extraArchives = [ "${lkpi.bindingsGen}/lib/libhelpers.a" "${clib}/lib/libsflkpiextras.a" ];
  };
  probe = pkgs.runCommand "sf-lkpi-extras-probe" { } ''
    mkdir -p $out
    ln -s ${probeObject}/sfextras_probe.o $out/
    ${nm} -u ${probeObject}/sfextras_probe.o | sed 's/^ *//' | grep '^_' | sed 's/^_//' | sort -u > $out/undefined
    ${nm} ${probeObject}/sfextras_probe.o | grep -E ' [TtSsDdBb] _(rust_helper_|pd_)' | sed 's/.* _//' | sort -u > $out/defined-helpers
    # what this directory and the helpers must supply is all there: no rust_helper_* and no
    # sf_xa_* function is left for the kernel collection to (fail to) bind. (Other pd_*
    # names in the list are the layer's own.)
    if grep -E '^(rust_helper_|sf_xa_|sf___xa_)' $out/undefined; then
      echo "unresolved after linking the probe with libhelpers.a and libsflkpiextras.a" >&2; exit 1
    fi
    # the helpers and functions of this directory that the probe pulled in
    for s in ${lib.concatStringsSep " " probeExpect}; do
      grep -qx "$s" $out/defined-helpers || { echo "not linked into the probe: $s" >&2; exit 1; }
    done
    ${crossToolchain}/bin/arm64-apple-darwin20.4-otool -l ${probeObject}/sfextras_probe.o | grep -E 'sectname|segname' | paste - - | sort -u > $out/sections
    # Where each remaining symbol is defined, by reading the layer's sources (a kext leaves
    # its undefined symbols for the kernel collection, so linking does not check this). A
    # definition is a line starting with the name and an opening parenthesis, as FreeBSD and
    # this layer write them. "not found" is xnu, libkern, or really missing: read the list.
    F=${layerTree}/src/Kernel/fbsdkpi
    while read -r s; do
      if grep -rqsE "^\**$s[[:space:]]*\(" --include='*.c' --include='*.cpp' $F; then
        echo "layer	$s"
      else
        echo "not-in-layer-sources	$s"
      fi
    done < $out/undefined > $out/undefined-where
    echo "probe links; $(wc -l < $out/undefined) symbols left for the kernel collection:"
    echo "  defined in the layer's sources: $(grep -c '^layer' $out/undefined-where)"
    echo "  not found there (xnu/libkern or missing): $(grep '^not' $out/undefined-where | cut -f2 | tr '\n' ' ')"
  '';
  probeExpect = [
    "sf_xa_load" "sf_xa_find" "sf___xa_alloc" "sf___xa_cmpxchg_raw" "sf___xa_erase"
    "rust_helper_xa_load" "rust_helper_xa_find" "rust_helper___xa_alloc" "rust_helper___xa_cmpxchg_raw"
    "rust_helper_xa_zero_entry" "rust_helper_xa_lock" "rust_helper_xa_unlock"
    "rust_helper_shrinker_alloc" "rust_helper_shrinker_register" "rust_helper_shrinker_free"
    "rust_helper_alloc_workqueue_noprof" "rust_helper_capable" "rust_helper_read_sanitised_ftr_reg"
    "rust_helper_seq_printf" "rust_helper_sf_boot_arg_str"
    "rust_helper_mmgrab" "rust_helper_mmdrop" "rust_helper_add_mm_counter"
  ];

  # ---- host tests ------------------------------------------------------------------------------
  hostTests = pkgs.stdenv.mkDerivation {
    name = "sf-lkpi-extras-host-tests";
    src = lib.cleanSourceWith { src = ./.; filter = path: type: baseNameOf path != "build"; };
    buildPhase = ''
      runHook preBuild
      make test LAYER=${layerTree} | tee test.log
      runHook postBuild
    '';
    installPhase = "mkdir -p $out; cp test.log $out/";
    dontFixup = true;
  };
in
{
  inherit clib matrix matrixBefore report kernel kernelAgx probeLib probeObject probe hostTests extrasModules;
  # the "before" derivations, for inspection
  inherit before;
}
