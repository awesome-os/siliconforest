# SPDX-License-Identifier: GPL-2.0
# SFRustPlat: the layer as a LinuxKPI DRM kext links it, the tree's own IOKit class for a
# device-tree platform device (fbsdkpi/xnu/SFDRMPlatformDevice.cpp, unchanged), and one
# prelinked object: the Rust `kernel` crate with its driver-core modules, the test driver and
# the C helpers they call (rust/sfrustplat.o, from ../../default.nix).
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/fbsdkpi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/drm/drm.cmake")

sf_drm_layout("${SF_DRMKMOD_DIR}" drminc drmdefs drmopts)
sf_drm_core_includes("${SF_DRMKMOD_DIR}" coreinc)

add_fbsdkpi_driver(SFRustPlat
    CLASS SFRustPlat
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFRustPlat
    INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    LINUXKPI_DRM
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/glue.c"
        "${SF_DRM_DIR}/src/sf_drm_printf.c"
    LKPI_INCLUDES ${drminc}
    LKPI_DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"linuxkpi\"
    LKPI_OPTIONS ${drmopts}
    DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"rustplat\"
    INCLUDES ${drminc} ${coreinc}
    XNU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/xnu/SFDRMPlatformDevice.cpp"
)

set(SFRUSTPLAT_RUST_OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/rust/sfrustplat.o)
if(NOT EXISTS ${SFRUSTPLAT_RUST_OBJECT})
    message(FATAL_ERROR "SFRustPlat: ${SFRUSTPLAT_RUST_OBJECT} is missing (lkpi/default.nix `platObject`)")
endif()
set_source_files_properties(${SFRUSTPLAT_RUST_OBJECT} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED FALSE)
target_sources(SFRustPlat.kext PRIVATE ${SFRUSTPLAT_RUST_OBJECT})
