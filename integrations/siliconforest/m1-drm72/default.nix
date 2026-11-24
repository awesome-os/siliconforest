# SF-F item F9: the DRM C core of SiliconForest's FreeBSD-derived Linux layer at the Linux 7.2.11
# API level (scheduler, GPUVM, drm_exec, dma-fence, GEM shmem helper, small GEM/syncobj/drv
# additions), as a patch series, and the proof that it compiles, links and satisfies the
# unmodified 7.2 Rust wrappers.
#
#   drmkmod      the flake's drm-kmod source (already with SiliconForest's removals and patch)
#                + patches/drm-kmod/*.patch
#   tree         the flake's arm64 DRM-kext tree slice (pinned, e7aa00a6) + patches/siliconforest/*.patch
#   kext         SFDRMmsm.kext and SFDRMDri.kext built from the two, as the flake builds
#                drm-msm-arm64, with the GEM shmem helper compiled in (SF_DRM_GEM_SHMEM=ON);
#                fails if the 7.2 code left an undefined symbol the 6.14 build did not have
#   kextDefault  the same without the option: what the tree's own kexts get
#   layerTree, kextOnLayer
#                the same two on ../m1-rust-kext/lkpi's patched layer (16 KiB PAGE_SIZE, allocator,
#                lock storage) instead of the pinned one: the combination the GPU kext will use
#   bindingsGen  the Rust-for-Linux bindgen step of ../m1-rust-kext/lkpi over the patched headers
#                (that recipe's layer, so `layerTree`)
#   matrix       the unmodified 7.2 wrappers compiled against those bindings, one module at a time
#
# Everything about bindgen, the C flag set, the Rust toolchain and the kernel crate build is
# ../m1-rust-kext/lkpi, imported, not copied: its derivations are re-instantiated with the
# patched source paths substituted for the pinned ones (see `subst`).
let
  pins = import ../m1-rust-kext/pins.nix;
  lkpi = import ../m1-rust-kext/lkpi;
  inherit (pins) pd pkgs crossToolchain;
  lib = pkgs.lib;

  applySeries = name: base: dir: pkgs.runCommand name { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -r ${base} $out
    chmod -R u+w $out
    for p in ${dir}/*.patch; do
      echo "applying $(basename $p)"
      # -F0: the series is exact against the pinned sources, drift must fail, not fuzz
      patch -d $out -p1 -F0 --no-backup-if-mismatch < $p
    done
  '';
  drmkmod = applySeries "siliconforest-drm-kmod-src-drm72" lkpi.drmkmod ./patches/drm-kmod;
  # the pinned tree slice, and the one ../m1-rust-kext/lkpi patches for its own ticket (the
  # same thing until that recipe grows a `pristineTree`)
  pinnedTree = lkpi.pristineTree or lkpi.tree;
  tree = applySeries "siliconforest-drm-tree-drm72" pinnedTree ./patches/siliconforest;
  layerTree = applySeries "siliconforest-drm-tree-lkpi-layer-drm72" lkpi.tree ./patches/siliconforest;

  # A derivation of ../m1-rust-kext with every occurrence of the pinned drm-kmod and tree
  # paths (and of earlier re-instantiated derivations) replaced. The recipes there refer to
  # their inputs only through such paths in their attributes, so this is the same build on
  # other sources.
  # The replaced paths are also taken out of the string's dependency context, so the result
  # does not wait for (or fail with) the build it no longer uses.
  substString = pairs: s:
    let
      from = map (p: "${p.from}") pairs;
      to = map (p: "${p.to}") pairs;
      r = builtins.replaceStrings from to s;
      dropped = lib.concatMap (f: builtins.attrNames (builtins.getContext f)) from;
    in builtins.appendContext (builtins.unsafeDiscardStringContext r)
      (builtins.removeAttrs (builtins.getContext r) dropped);
  subst = pairs: drv: drv.overrideAttrs (old: lib.mapAttrs (n: v:
      if builtins.isString v then substString pairs v
      else map (e: if builtins.isString e then substString pairs e else e) v)
    (lib.filterAttrs (n: v: builtins.isString v || builtins.isList v)
      (builtins.removeAttrs old [ "name" "pname" "version" "outputs" "nativeBuildInputs" "buildInputs" "passthru" "meta" ])));
  srcPairs = [ { from = lkpi.drmkmod; to = drmkmod; } { from = lkpi.tree; to = layerTree; } ];

  nm = "${crossToolchain}/bin/arm64-apple-darwin20.4-nm";
  # `baseTree` with the pinned drm-kmod is the 6.14 build the result is compared with
  mkKext = { suffix, baseTree ? pinnedTree, tree, cmakeExtra, expectDefined }:
    let
      oldFlag = "-DSF_DRMKMOD_DIR=${lkpi.drmkmod}";
      newFlag = "-DSF_DRMKMOD_DIR=${drmkmod}${cmakeExtra}";
      baseKext = lkpi.drmKext.overrideAttrs (old: { src = baseTree; });
    in lkpi.drmKext.overrideAttrs (old:
      let configurePhase = builtins.replaceStrings [ oldFlag ] [ newFlag ] old.configurePhase; in
      # the flake must still pass the drm-kmod directory the way this expects
      assert configurePhase != old.configurePhase;
      {
        pname = "siliconforest-drm-msm-arm64-drm72${suffix}";
        src = tree;
        inherit configurePhase;
        installPhase = old.installPhase + ''
          # what ninja ran for the DRM core (flags and the file list)
          mkdir -p $out/nix-support
          ninja -C build-nix -t commands SFDRMmsm.kext > $out/nix-support/build-commands
          bin=$out/System/Library/Extensions/SFDRMmsm.kext/Contents/MacOS/SFDRMmsm
          base=${baseKext}/System/Library/Extensions/SFDRMmsm.kext/Contents/MacOS/SFDRMmsm
          # A kext is linked with its undefined symbols left for the kernel collection to
          # bind, so "it linked" says nothing by itself. The 7.2 code must not need a symbol
          # the 6.14 build of the same kext on the same layer did not already import.
          ${nm} -u $base | sort -u > undefined.base
          ${nm} -u $bin | sort -u > undefined.new
          comm -13 undefined.base undefined.new > $out/nix-support/undefined-added
          comm -23 undefined.base undefined.new > $out/nix-support/undefined-removed
          cp undefined.new $out/nix-support/undefined
          if [ -s $out/nix-support/undefined-added ]; then
            echo "symbols the 7.2 DRM core needs and nothing defines:" >&2
            cat $out/nix-support/undefined-added >&2
            exit 1
          fi
          ${nm} $bin | grep -E ' [TtDdSsBb] ' | awk '{print $3}' | sort -u > defined
          for s in ${lib.concatStringsSep " " expectDefined}; do
            grep -qx "_$s" defined || { echo "not in the kext: $s" >&2; exit 1; }
          done
          echo "undefined symbols: $(wc -l < undefined.new) (pinned build: $(wc -l < undefined.base)), none added"
        '';
      });
  coreSyms = [
    # scheduler
    "drm_sched_init" "drm_sched_fini" "drm_sched_job_init" "drm_sched_job_arm" "drm_sched_entity_init"
    "drm_sched_entity_push_job" "drm_sched_entity_destroy" "drm_sched_job_add_dependency"
    "drm_sched_job_cleanup" "drm_sched_rq_init"
    # gpuvm
    "drm_gpuvm_init" "drm_gpuvm_sm_map" "drm_gpuvm_sm_unmap" "drm_gpuvm_bo_unmap" "drm_gpuva_unlink_defer"
    "drm_gpuvm_bo_deferred_cleanup" "drm_gpuvm_bo_put_deferred" "drm_gpuvm_bo_obtain_prealloc"
    "drm_gpuvm_exec_lock" "drm_exec_lock_obj"
    # dma-fence, gem, syncobj, drv
    "dma_fence_init64" "dma_fence_check_and_signal" "dma_fence_driver_name"
    "drm_syncobj_set_wait_hint" "__drm_dev_alloc"
  ];
  shmemSyms = [
    "drm_gem_shmem_init" "drm_gem_shmem_release" "drm_gem_shmem_get_pages_sgt" "drm_gem_shmem_vmap_locked"
    "drm_gem_shmem_vunmap_locked" "drm_gem_shmem_madvise_locked" "drm_gem_shmem_purge_locked"
    "drm_gem_shmem_idle_pages_releasable_locked" "drm_gem_shmem_release_idle_pages_locked"
    "__drm_gem_shmem_free_sgt_locked" "drm_gem_shmem_prime_import_sg_table" "drm_gem_shmem_dumb_create"
    "drm_gem_shmem_vm_ops" "drm_gem_shmem_mmap" "drm_gem_shmem_sf_mmap_pages"
  ];
  shmemOn = " -DSF_DRM_GEM_SHMEM=ON";
  kext = mkKext { suffix = "-shmem"; inherit tree; cmakeExtra = shmemOn; expectDefined = coreSyms ++ shmemSyms; };
  kextDefault = mkKext { suffix = ""; inherit tree; cmakeExtra = ""; expectDefined = coreSyms; };
  kextOnLayer = mkKext {
    suffix = "-shmem-lkpi-layer"; baseTree = lkpi.tree; tree = layerTree;
    cmakeExtra = shmemOn; expectDefined = coreSyms ++ shmemSyms;
  };

  rust = import ./rust.nix { inherit pins lkpi lib pkgs drmkmod subst srcPairs; };
in
{
  inherit drmkmod tree layerTree kext kextDefault kextOnLayer;
  inherit (rust) bindingsGen helpersDir rustTree bindings uapi matrix;
}
