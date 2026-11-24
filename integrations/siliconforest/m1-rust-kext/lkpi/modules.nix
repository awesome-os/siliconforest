# Which parts of the `kernel` crate are built, and the configuration they see.
# A module is on when its name is in a list here (tools/gate.py gave every `pub mod` in
# rust/kernel/lib.rs a #[cfg(sf_mod_<name>)]); nothing in the Rust tree is edited to choose.
rec {
  # B2 "core tier": no subsystem behind it beyond LinuxKPI's own runtime.
  core = [
    "alloc" "bits" "build_assert" "error" "ffi" "fmt" "init" "list" "num" "prelude" "print"
    "ptr" "safety" "sizes" "std_vendor" "str" "sync" "time" "transmute" "types"
    # needed by the above: alloc uses page (PAGE_SIZE, Vmalloc pages) which uses addr;
    # sync::CondVar and prelude's `current!` use task
    "addr" "page" "task"
    # B3
    "workqueue"
  ];

  # Phase C: what SFRustLKPI is built with. io is the register access (Mmio, Region), not
  # io::mem; irq is Flags only; see patches/0005.
  phaseC = core ++ [ "io" "processor" "irq" "uaccess" ];

  # Phase D: the driver-core tier, unmodified device/driver/platform/devres/of/io::mem/
  # irq::request/dma on the C-side driver-core surface (patches-layer/0005).
  plat = phaseC ++ [ "device" "device_id" "driver" "platform" "devres" "of" "acpi" "dma"
    "revocable" "impl_flags" ];

  # D4: everything the Asahi driver (common code + G13) is written against.
  agx = plat ++ [ "drm" "drm_device" "drm_driver" "drm_file" "drm_ioctl" "drm_gem" "drm_mm"
    "drm_sched" "drm_gpuvm" "drm_syncobj" "dma_fence" "dma_buf" "scatterlist" "ioctl" "soc"
    "soc_apple_rtkit" "soc_apple_mailbox" "bitfield"
    # m1-lkpi-extras
    "xarray" "module_param" "mm" "seq_file" ];
  agxConfig = [ "CONFIG_DRM" ''CONFIG_DRM="y"'' "CONFIG_DMA_SHARED_BUFFER"
    "CONFIG_RUST_DRM_GEM_SHMEM_HELPER" "CONFIG_RUST_DRM_GPUVM"
    ''CONFIG_RUST_APPLE_RTKIT="y"'' ''CONFIG_RUST_APPLE_MAILBOX="y"'' ];

  # Kconfig as rustc sees it (Linux: include/generated/rustc_cfg). Only what the tree's
  # CONFIG_ cfgs can truthfully be on this platform.
  config = [
    "CONFIG_RUST" "CONFIG_64BIT" "CONFIG_ARM64" "CONFIG_SMP" "CONFIG_PRINTK"
    "CONFIG_ARCH_SUPPORTS_ATOMIC_RMW"
    # ioremap exists (helpers/io.c)
    "CONFIG_HAS_IOMEM"
    # device-tree matching (kernel::driver looks ids up only with it)
    "CONFIG_OF"
    # init/Kconfig, for rustc 1.95
    "CONFIG_RUSTC_HAS_SPAN_FILE" "CONFIG_RUSTC_HAS_UNNECESSARY_TRANSMUTES"
    "CONFIG_RUSTC_HAS_FILE_WITH_NUL" "CONFIG_RUSTC_HAS_FILE_AS_C_STR"
    ''CONFIG_RUSTC_VERSION_TEXT="rustc 1.95.0"''
    # this build: LinuxKPI underneath (the hand patches and gate_errnos.py key on it)
    "sf_lkpi"
  ];

  cfgsFor = modules: config ++ map (m: "sf_mod_${m}") modules;

  # B4: the subsystem modules the Asahi driver uses, each compiled on top of `core`
  # (tools/matrix.py). `modules` are gate names: top-level modules, drm_<sub> for
  # rust/kernel/drm/<sub>, soc_apple_<sub> for rust/kernel/soc/apple/<sub>.
  matrixTargets =
    let
      drmCfgs = [ "CONFIG_DRM" ''CONFIG_DRM="y"'' "CONFIG_DMA_SHARED_BUFFER" ];
      top = name: { inherit name; modules = [ name ]; cfgs = [ ]; };
      drm = sub: { name = "drm::${sub}"; modules = [ "drm" "drm_${sub}" ]; cfgs = drmCfgs; };
    in
    map top [
      "device_id" "device" "driver" "platform" "of" "io" "processor" "uaccess" "dma" "debugfs"
      "maple_tree" "module_param" "xarray" "devres" "irq" "ioctl" "mm" "fs" "cred"
    ] ++ [
      { name = "cpufreq"; modules = [ "cpufreq" ]; cfgs = [ "CONFIG_CPU_FREQ" ]; }
      { name = "time::hrtimer"; modules = [ "hrtimer" ]; cfgs = [ ]; }
      { name = "dma_fence"; modules = [ "dma_fence" ]; cfgs = [ "CONFIG_DMA_SHARED_BUFFER" ]; }
    ] ++ map drm [ "device" "driver" "file" "ioctl" "gem" "mm" "sched" "syncobj" ] ++ [
      { name = "drm::gpuvm"; modules = [ "drm" "drm_gpuvm" ]; cfgs = drmCfgs ++ [ "CONFIG_RUST_DRM_GPUVM" ]; }
      { name = "drm::gem::shmem"; modules = [ "drm" "drm_gem" ]; cfgs = drmCfgs ++ [ "CONFIG_RUST_DRM_GEM_SHMEM_HELPER" ]; }
      { name = "soc::apple::rtkit"; modules = [ "soc" "soc_apple_rtkit" ]; cfgs = [ ''CONFIG_RUST_APPLE_RTKIT="y"'' ]; }
      { name = "soc::apple::mailbox"; modules = [ "soc" "soc_apple_mailbox" ]; cfgs = [ ''CONFIG_RUST_APPLE_MAILBOX="y"'' ]; }
    ];
}
