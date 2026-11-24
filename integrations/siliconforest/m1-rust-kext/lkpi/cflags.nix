# The C flag set of LinuxKPI/DRM code inside a SiliconForest arm64 kext, in ONE place.
#
# It reproduces what cmake gives such a file: `_fbsdkpi_objects` (fbsdkpi.cmake) plus
# `sf_drm_layout`/`sf_drm_core_includes` (drm/drm.cmake), as SFDRMmsm's drm objects get them.
# bindgen, the C helpers and the status tables all use this list; ../build-lkpi-kext.nix
# compares its -D set against the command ninja really runs for the kext and fails on a
# difference, because a differing define means differing struct layouts between Rust and C.
#
# LinuxKPI is not a shared kext here: every kext compiles its own copy (Haiku's FreeBSD
# compatibility layer, FreeBSD LinuxKPI, SiliconForest glue, and for DRM kexts the drm-kmod
# core) with -fvisibility=hidden, so nothing of it enters the collection's symbol namespace.
{ lib, tree, drmkmod, sdk }:
let
  F = "${tree}/src/Kernel/fbsdkpi";
  lk = "${F}/freebsd/linuxkpi/common/include/linux";
in
rec {
  # SF_DRM_KCONFIG for arm64 (drm.cmake)
  kconfig = [
    "APERTURE_HELPERS" "ARCH_HAVE_NMI_SAFE_CMPXCHG" "BACKLIGHT_CLASS_DEVICE" "FB"
    "NEED_SG_DMA_LENGTH" "PCI" "PM" "PM_SLEEP" "SMP" "SUSPEND" "VIDEO_CMDLINE" "64BIT"
    "DRM_CLIENT" "DRM_CLIENT_SETUP" "DRM_EXEC" "DRM_GPUVM" "DRM_AMDGPU_CIK" "ARM64"
  ];
  defines = map (c: "CONFIG_${c}") kconfig ++ [
    "CONFIG_DRM_FBDEV_OVERALLOC=100"
    "LINUXKPI_VERSION=61400" "SF_LKPI_DRM=1"
    "FBSD_DRIVER=1" "_XOPEN_SOURCE"
    "_KERNEL=1" "_KERNEL_MODE=1" "__HAIKU__=1" "__SILICONFOREST__=1" "panic=fbsd_panic"
    # the toolchain wrapper and the kext directory add these two
    "XNU_PLATFORM_MacOSX=1" "TARGET_OS_MACOSX=1"
  ];
  # Linux Kconfig symbols that only rust/helpers/*.c test (#ifdef around whole files). The
  # layer has the code (drm core, dma-buf, gpuvm) but no Kconfig for it. No header of the
  # include set looks at these, so they cannot change a struct layout; they are given to
  # the helper compiles and their bindgen run only, never to LinuxKPI itself.
  helperDefines = [ "CONFIG_DRM" "CONFIG_DMA_SHARED_BUFFER" "CONFIG_RUST_DRM_GPUVM" "CONFIG_DRM_GEM_SHMEM_HELPER" ];
  # per object library in cmake; any value does for headers
  moduleDefines = [ ''KBUILD_MODNAME="rust"'' "LINUXKPI_PARAM_PREFIX=rust_" ];
  includes = [
    # a drm kext's own headers first, so every file sees the same structs
    "${F}/drm/include"
    "${drmkmod}/linuxkpi/bsd/include"
    "${F}/sf/lkpi-override"
    "${F}/freebsd/linuxkpi/common/include"
    "${F}/freebsd/linuxkpi/dummy/include"
    "${F}/sf/fbsd-extra"
    "${F}/freebsd"
    "${F}/haiku/freebsd_iflib/compat"
    # drm core
    "${drmkmod}/drivers/gpu/drm"
    "${drmkmod}/include"
    "${drmkmod}/include/drm"
    "${drmkmod}/include/uapi"
    "${drmkmod}/drivers/gpu"
    "${drmkmod}/drivers/gpu/drm/scheduler"
    # Haiku's FreeBSD compatibility layer under LinuxKPI
    "${F}/haiku/freebsd_network/compat"
    "${F}/sf/haiku-api"
    "${F}/haiku/freebsd_network"
    "${F}/sf"
    "${F}"
    "${F}/sf/haiku-headers/private-kernel"
    "${F}/sf/haiku-headers"
    "${F}/sf/haiku-headers/os"
    "${F}/sf/haiku-headers/os/drivers"
    "${F}/sf/haiku-headers/os/kernel"
    "${F}/sf/haiku-headers/os/storage"
    "${F}/sf/haiku-headers/os/support"
    "${F}/sf/haiku-headers/posix"
  ];
  # in command-line order
  forcedIncludes = [
    "${tree}/src/Kernel/xnu/sf_apple_builtins.h"
    "${F}/sf/fbsd-extra/sf_lkpi_pre.h"
    "${lk}/compiler_types.h"
    "${lk}/kconfig.h"
    "${F}/drm/include/sf_drm_pre.h"
  ];
  lkpiPre = "${F}/sf/fbsd-extra/sf_lkpi_pre.h";
  options = [
    "-Wno-undef-prefix" "-ffreestanding" "-nostdlibinc" "-fno-common" "-fno-stack-protector"
    "-fvisibility=hidden" "-std=gnu11" "-Wno-everything"
    # linux zeroes stack locals by default and drm relies on it
    "-ftrivial-auto-var-init=zero"
    "-Werror=implicit-function-declaration"
  ];
  sysroot = "${sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";

  # for the cross toolchain's clang wrapper (it adds -target, -mcpu=apple-a10, -isysroot)
  cc = [ "-arch" "arm64" ]
    ++ map (d: "-D${d}") (defines ++ moduleDefines)
    ++ map (i: "-I${i}") includes
    ++ lib.concatMap (h: [ "-include" h ]) forcedIncludes
    ++ options;

  # for libclang under bindgen, which has no wrapper: the wrapper's own arguments spelled
  # out, and without -fvisibility=hidden (bindgen skips hidden functions)
  bindgen = [ "-target" "arm64-apple-macosx26.5" "-mcpu=apple-a10" "-isysroot" sysroot ]
    ++ lib.filter (a: a != "-fvisibility=hidden") (lib.drop 2 cc);
}
