# Rust for XNU arm64 kexts: THE one place for the target specification and the rustc flag set.
#
# Nothing else in this directory names a rustc flag that affects code generation. Phase B
# (bindgen output, the `kernel` crate) adds crates with `mkCrate` below and inherits all of it.
#
# The build mirrors Linux's rust/Makefile (7.2): no cargo, RUSTC_BOOTSTRAP=1 on a stable
# rustc, `core` (and here also the real `compiler_builtins` and `alloc`) compiled from the
# compiler's own library sources for a custom target, then our crates against that.
# Differences from Linux, and why, are noted at each point.
{ pd, pkgs, crossToolchain }:
let
  lib = pkgs.lib;

  # nixpkgs' rustc (the flake's own nixos-26.05 pin): 1.95.0 on LLVM 21.1.8, the same LLVM
  # major as the clang 21.1.8 that compiles the C/C++ side. Nothing was added to the pin.
  rustc = pkgs.rustc.unwrapped;
  # library/ of the same compiler: core, alloc, compiler-builtins sources
  rustLibSrc = pkgs.rustPlatform.rustLibSrc;

  targetName = "aarch64-apple-xnukext";

  # Target specification. Start: `rustc --print target-spec-json --target aarch64-apple-darwin`
  # (the userland target for the same triple the kexts' clang uses). Reference for every
  # choice: the cc1 line the tree's kext compiles expand to, i.e.
  #   arm64-apple-darwin20.4-clang++ -arch arm64 -fapple-kext -fno-stack-protector -fno-common
  #   => -triple arm64-apple-macosx26.5.0 -target-cpu apple-a10 -mframe-pointer=non-leaf
  #      -disable-red-zone -target-abi darwinpcs -ffreestanding, relocation model static/pic
  #      (LLVM forces PIC on arm64 Darwin either way), no stack protector, no stack probes,
  #      NEON/FP left on (+neon +fp-armv8; XNU arm64 has no -mgeneral-regs-only).
  targetSpec = {
    # --- identity: must stay Apple/Mach-O -------------------------------------------------
    # The version is appended by rustc from MACOSX_DEPLOYMENT_TARGET (see `env`), giving the
    # kexts' arm64-apple-macosx26.5. arm64, NOT arm64e: no pointer authentication.
    llvm-target = "arm64-apple-macosx";
    arch = "aarch64";
    # os/vendor select the Darwin C ABI in rustc itself (darwinpcs argument packing, variadic
    # arguments on the stack) and in `core::ffi` (c_char is i8 on Apple, u8 on Linux arm64).
    os = "macos";
    vendor = "apple";
    is-like-darwin = true;
    abi-return-struct-as-int = true;
    data-layout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32";
    target-pointer-width = 64;
    binary-format = "mach-o";
    archive-format = "darwin";
    llvm-floatabi = "hard";
    max-atomic-width = 128;
    # No `target-family = [ "unix" ]`: there is no libc under a kext, cfg(unix) must be false.

    # --- code generation: mirror the kext C/C++ flags --------------------------------------
    # toolchain.nix wraps clang with -mcpu=apple-a10 (ARMv8.0 baseline, no LSE atomics):
    # atomics become ldxr/stxr loops exactly as in the C kexts.
    cpu = "apple-a10";
    # -mframe-pointer=non-leaf (also XNU's -momit-leaf-frame-pointer): the kernel's
    # backtracer walks x29 chains through kext frames.
    frame-pointer = "non-leaf";
    # -disable-red-zone, which -fapple-kext/-mkernel imply on arm64.
    disable-redzone = true;
    # arm64 Darwin is always PIC in LLVM; kexts are slid and bound by kc-builder through GOT.
    relocation-model = "pic";
    code-model = "small";
    # No unwinding in the kernel: panics abort (the #[panic_handler] calls panic()).
    panic-strategy = "abort";
    # kext.cmake: -fno-stack-protector (no usable guard for kexts), and clang emits no
    # stack probes for kexts either (userland Darwin Rust would use inline probes).
    stack-probes = { kind = "none"; };
    # No TLS in the kernel: #[thread_local] becomes a compile error instead of a
    # __thread_vars section kc-builder cannot bind.
    has-thread-local = false;
    # Never asked to link: only rlibs and a staticlib come out of rustc.
    dynamic-linking = false;
    eh-frame-header = false;
    emit-debug-gdb-scripts = false;
    function-sections = false;
    debuginfo-kind = "dwarf-dsym";
    linker-flavor = "darwin-cc";
    link-env = [ "ZERO_AR_DATE=1" ];
    metadata = { description = "XNU arm64 kernel extension (SiliconForest)"; host_tools = false; std = false; tier = 3; };
  };

  targetJson = pkgs.writeTextDir "${targetName}.json" (builtins.toJSON targetSpec);
  targetFile = "${targetJson}/${targetName}.json";

  # Environment every rustc invocation for the target runs in.
  env = {
    # stable rustc, unstable features (custom target JSON, building core/alloc): as Linux does
    RUSTC_BOOTSTRAP = "1";
    # the kexts' clang target is arm64-apple-macosx26.5; rustc stamps this into the triple and
    # LC_BUILD_VERSION, so ld64 sees Rust and C objects built for the same OS version
    MACOSX_DEPLOYMENT_TARGET = "26.5";
  };

  # Flags for every crate compiled for the target (core, alloc, ours, phase B's).
  rustFlags = [
    "--target=${targetFile}"
    # custom target specifications need this since 1.95 (Linux rust/Makefile says the same)
    "-Zunstable-options"
    # -O2 like the kexts' release build
    "-Copt-level=2"
    # no unwinding (also in the target spec; repeated so an rlib built otherwise cannot link in)
    "-Cpanic=abort"
    # clang gives C kext code -funwind-tables=1 (compact unwind, never consumed in the kernel);
    # for Rust emit none: no __eh_frame, no __compact_unwind, no personality references.
    # Linux arm64 passes the same flag.
    "-Cforce-unwind-tables=no"
    # one object per crate, and no LLVM bitcode in the rlibs: no LTO. The C side is not built
    # with LTO either, and ld64 here has no libLTO; cross-language inlining of C helpers
    # (Linux's llvm-link step) is a phase B decision, rustc and clang are both LLVM 21.1.8.
    "-Ccodegen-units=1"
    "-Cembed-bitcode=no"
    # v0 mangling, as Linux: demangles unambiguously in panic logs and symbol tables
    "-Csymbol-mangling-version=v0"
    "-Cdebuginfo=0"
    "-Cdebug-assertions=no"
    "-Coverflow-checks=no"
    # reproducible paths in panic locations instead of /nix/store/...-rust-lib-src
    "--remap-path-prefix=${rustLibSrc}=library"
  ];

  # `--cfg`s for core. Linux: no_fp_fmt_parse (drop float formatting/parsing from core).
  # FP registers are usable in XNU arm64 kexts, so this is size, not correctness.
  coreCfgs = [ "no_fp_fmt_parse" ];

  rustcEnvExports = lib.concatStringsSep "\n" (lib.mapAttrsToList (k: v: "export ${k}=${lib.escapeShellArg v}") env);
  flagsStr = lib.escapeShellArgs rustFlags;

  # Step 1: core + compiler_builtins + alloc for the target, laid out as a rustc sysroot
  # ($out/lib/rustlib/<target>/lib/lib*.rlib). Depends only on rustc, its library sources,
  # the target spec and the flags above: phase B never rebuilds it by adding crates.
  #
  # Unlike Linux, compiler_builtins is the real crate, not a panicking stub: there is no
  # libgcc/compiler-rt export in the XNU KPI for kexts to fall back on (__udivti3 etc.).
  # Its `mem` feature stays off: memcpy/memmove/memset/memcmp/bcmp come from the kernel
  # (com.apple.kpi.libkern). Its cfgs come from running its own build.rs, as cargo would.
  sysroot = pkgs.stdenv.mkDerivation {
    name = "rust-sysroot-${targetName}-${rustc.version}";
    dontUnpack = true;
    nativeBuildInputs = [ rustc ];
    buildPhase = ''
      runHook preBuild
      ${rustcEnvExports}
      L=$out/lib/rustlib/${targetName}/lib
      mkdir -p "$L"
      R=(rustc ${flagsStr} --sysroot "$out" --cap-lints=allow --edition=2024 --crate-type rlib --out-dir "$L")

      echo "== core"
      "''${R[@]}" ${lib.concatMapStringsSep " " (c: "--cfg ${c}") coreCfgs} \
        --crate-name core ${rustLibSrc}/core/src/lib.rs

      echo "== compiler_builtins (cfgs from its build.rs)"
      cb=${rustLibSrc}/compiler-builtins/compiler-builtins
      rustc --edition=2024 --cap-lints=allow -Clinker=cc --cfg 'feature="compiler-builtins"' \
        --crate-name cb_build "$cb/build.rs" -o cb_build
      declare -A cfg
      while IFS= read -r line; do
        k=''${line%%=*}; v=
        if [[ $line == *=* ]]; then v=''${line#*=}; v=''${v%\"}; v=''${v#\"}; fi
        K=CARGO_CFG_$(printf %s "$k" | tr a-z A-Z)
        if [[ -n ''${cfg[$K]:-} ]]; then cfg[$K]+=",$v"; else cfg[$K]=$v; fi
      done < <("''${R[@]}" --print cfg)
      (
        for K in "''${!cfg[@]}"; do export "$K=''${cfg[$K]}"; done
        export TARGET=${targetName} OPT_LEVEL=2 CARGO_FEATURE_COMPILER_BUILTINS=1
        : "''${CARGO_CFG_TARGET_ENV=}"; export CARGO_CFG_TARGET_ENV
        ./cb_build
      ) > cb_build.out
      cbcfgs=()
      while IFS= read -r c; do cbcfgs+=(--cfg "$c"); done \
        < <(sed -n -E 's/^cargo::?rustc-cfg=(.*)$/\1/p' cb_build.out)
      printf '   %s\n' "''${cbcfgs[@]}"
      # the `mem` feature must stay off (see above); refuse a build.rs that turns it on
      if printf '%s\n' "''${cbcfgs[@]}" | grep -qx 'feature="mem"'; then
        echo "compiler_builtins build.rs enabled feature=mem for ${targetName}" >&2; exit 1
      fi
      "''${R[@]}" --cfg 'feature="compiler-builtins"' "''${cbcfgs[@]}" \
        --crate-name compiler_builtins "$cb/src/lib.rs"

      echo "== alloc"
      "''${R[@]}" --crate-name alloc ${rustLibSrc}/alloc/src/lib.rs

      mkdir -p $out/nix-support
      printf '%s\n' "''${cbcfgs[@]}" > $out/nix-support/compiler-builtins-cfgs
      cp ${targetFile} $out/nix-support/
      printf '%s\n' ${flagsStr} > $out/nix-support/rustflags
      runHook postBuild
    '';
    dontInstall = true;
    dontFixup = true;
  };

  # Step 2: one crate against the sysroot. `deps` are other mkCrate results (rlibs).
  # crateType "rlib" for libraries, "staticlib" for the one crate a kext links: rustc puts
  # that crate, all its rlib deps, core/alloc/compiler_builtins and the allocator shim
  # (__rust_alloc -> #[global_allocator]) into a single lib<name>.a.
  mkCrate =
    { crateName, src, root ? "lib.rs", crateType ? "rlib", edition ? "2024", deps ? [ ], extraFlags ? [ ]
      # phase B: `--cfg` values, host proc-macro crates ({ name; lib; } with lib the .so) and
      # their directory for crates that only depend on them indirectly, extra environment
    , cfgs ? [ ], procMacros ? [ ], procMacroDirs ? [ ], env ? { }
    }:
    let
      allDeps = lib.unique (lib.concatMap (d: [ d ] ++ d.passthru.rustTransitiveDeps) deps);
      extraFlags' = extraFlags
        ++ lib.concatMap (c: [ "--cfg" c ]) cfgs
        ++ lib.concatMap (m: [ "--extern" "${m.name}=${m.lib}" ]) procMacros
        ++ lib.concatMap (d: [ "-L" "dependency=${d}" ]) procMacroDirs;
      crateEnvExports = lib.concatStrings (lib.mapAttrsToList (k: v: "\nexport ${k}=${lib.escapeShellArg v}") env);
    in
    pkgs.stdenvNoCC.mkDerivation {
      name = "rust-${targetName}-${crateName}";
      dontUnpack = true;
      nativeBuildInputs = [ rustc ];
      buildPhase = ''
        runHook preBuild
        ${rustcEnvExports}${crateEnvExports}
        mkdir -p $out/lib
        rustc ${flagsStr} --sysroot ${sysroot} \
          --edition=${edition} --crate-type ${crateType} --crate-name ${crateName} \
          --remap-path-prefix=${src}=${crateName} \
          ${lib.concatMapStringsSep " " (d: "--extern ${d.passthru.crateName}=${d}/lib/lib${d.passthru.crateName}.rlib") deps} \
          ${lib.concatMapStringsSep " " (d: "-L dependency=${d}/lib") allDeps} \
          ${lib.escapeShellArgs extraFlags'} \
          --out-dir $out/lib ${src}/${root}
        runHook postBuild
      '';
      dontInstall = true;
      dontFixup = true;
      passthru = { inherit crateName crateType; rustTransitiveDeps = allDeps; };
    };

  # Step 3: staticlib -> ONE relocatable Mach-O object for the kext's CMake target.
  # `ld -r` with an export list does what Linux does with objcopy on compiler_builtins.o:
  # everything except the named C entry points becomes a local symbol. That matters here
  # because compiler_builtins defines its intrinsics as weak definitions; left global, ld64
  # would route calls to them through stubs with weak-bind entries that kc-builder has to
  # resolve at collection time. Local, they are plain direct branches inside the kext.
  mkKextObject =
    { name, staticlib, exports
      # phase B: static archives searched after the Rust one (the C helpers Rust calls)
    , extraArchives ? [ ]
    }:
    pkgs.runCommand "${name}.o" { } ''
      mkdir -p $out
      printf '%s\n' ${lib.escapeShellArgs exports} > exports.list
      ${crossToolchain}/bin/arm64-apple-darwin20.4-ld -r -arch arm64 \
        ${lib.concatMapStringsSep " " (s: "-u ${s}") exports} \
        -exported_symbols_list exports.list \
        ${staticlib}/lib/lib${staticlib.passthru.crateName}.a${lib.concatMapStrings (a: " ${a}") extraArchives} \
        -o $out/${name}.o
      cp exports.list $out/${name}.exports
    '';
in
{
  inherit rustc rustLibSrc targetName targetSpec targetFile env rustFlags sysroot mkCrate mkKextObject;
}
