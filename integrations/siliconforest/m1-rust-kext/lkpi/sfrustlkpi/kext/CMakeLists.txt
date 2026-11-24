# SPDX-License-Identifier: GPL-2.0
# SFRustLKPI: the layer exactly as a LinuxKPI DRM kext links it (Haiku's FreeBSD
# compatibility code, FreeBSD LinuxKPI with the DRM additions, SiliconForest glue, all compiled
# into this kext with hidden visibility), without a drm core or a driver, plus one
# prelinked object: the Rust `kernel` crate, the test crate and the C helpers they call
# (rust/sfrustlkpi.o, from ../../default.nix through ../../../build-lkpi-kext.nix).
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/fbsdkpi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/drm/drm.cmake")

# the struct layouts of a drm kext: the Rust bindings were generated with these defines
sf_drm_layout("${SF_DRMKMOD_DIR}" drminc drmdefs drmopts)
sf_drm_core_includes("${SF_DRMKMOD_DIR}" coreinc)

add_fbsdkpi_driver(SFRustLKPI
    CLASS SFRustLKPI
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFRustLKPI
    INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    LINUXKPI_DRM
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/glue.c"
        # LinuxKPI built with SF_LKPI_DRM prints through this (linux %p extensions)
        "${SF_DRM_DIR}/src/sf_drm_printf.c"
    LKPI_INCLUDES ${drminc}
    LKPI_DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"linuxkpi\"
    LKPI_OPTIONS ${drmopts}
    DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"rustlkpi\"
    INCLUDES ${drminc} ${coreinc}
    XNU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SFRustLKPI.cpp"
)

set(SFRUSTLKPI_RUST_OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/rust/sfrustlkpi.o)
if(NOT EXISTS ${SFRUSTLKPI_RUST_OBJECT})
    message(FATAL_ERROR "SFRustLKPI: ${SFRUSTLKPI_RUST_OBJECT} is missing (lkpi/default.nix `object`)")
endif()
set_source_files_properties(${SFRUSTLKPI_RUST_OBJECT} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED FALSE)
target_sources(SFRustLKPI.kext PRIVATE ${SFRUSTLKPI_RUST_OBJECT})
# No -dead_strip here (unlike SFRustHello): the layer's code is hidden-visibility and its
# SYSINITs are reached only through their section, so the linker would see them as dead.
