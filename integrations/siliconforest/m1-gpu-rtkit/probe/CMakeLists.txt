# SPDX-License-Identifier: GPL-2.0
# SFRtkitProbe: the Apple mailbox + RTKit port (src/, copied here as rtkit/ by
# ../build-probe-kext.nix) on the layer exactly as a LinuxKPI DRM kext links it, the way
# SFRustLKPI is built (../../m1-rust-kext/lkpi/sfrustlkpi/kext/CMakeLists.txt): Haiku's
# FreeBSD compatibility code, FreeBSD LinuxKPI with the DRM additions and SiliconForest glue,
# all hidden visibility, same defines as the Rust bindings (sf_drm_layout).
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/fbsdkpi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/drm/drm.cmake")

sf_drm_layout("${SF_DRMKMOD_DIR}" drminc drmdefs drmopts)
sf_drm_core_includes("${SF_DRMKMOD_DIR}" coreinc)

set(RTKIT "${CMAKE_CURRENT_SOURCE_DIR}/rtkit")
# ../m1-dcp-dart: the display DARTs behind Linux's IOMMU/DMA API (mode 5)
set(DCPDART "${CMAKE_CURRENT_SOURCE_DIR}/dcpdart")

add_fbsdkpi_driver(SFRtkitProbe
    CLASS SFRtkitProbe
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFRtkitProbe
    INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    LINUXKPI_DRM
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/glue.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/probe.c"
        "${RTKIT}/mailbox.c"
        "${RTKIT}/rtkit.c"
        "${RTKIT}/rtkit-crashlog.c"
        "${RTKIT}/sf_rtkit_compat.c"
        "${DCPDART}/src/sf_dart_table.c"
        "${DCPDART}/src/sf_dcp_iommu.c"
        # ../m1-gpu-dt/kernel/sf_of_iter.c: of_find_compatible_node() that honours `from`
        "${DCPDART}/sf_of_iter.c"
        # LinuxKPI built with SF_LKPI_DRM prints through this (linux %p extensions)
        "${SF_DRM_DIR}/src/sf_drm_printf.c"
    LKPI_INCLUDES ${drminc}
    LKPI_DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"linuxkpi\"
    LKPI_OPTIONS ${drmopts}
    DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"rtkitprobe\"
    # the port's own linux/soc/apple/*.h first
    INCLUDES "${RTKIT}/include" "${RTKIT}" "${CMAKE_CURRENT_SOURCE_DIR}" "${DCPDART}/include" ${drminc} ${coreinc}
    XNU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SFRtkitProbe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/sf_dcp_dart.cpp"
        "${DCPDART}/src/sf_dart.cpp"
        "${RTKIT}/SFRtkitPlatform.cpp"
)
target_include_directories(SFRtkitProbe.kext PRIVATE "${RTKIT}" "${CMAKE_CURRENT_SOURCE_DIR}" "${DCPDART}/include")
