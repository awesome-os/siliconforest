# SPDX-License-Identifier: GPL-2.0
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/fbsdkpi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/drm/drm.cmake")
sf_drm_layout("${SF_DRMKMOD_DIR}" drminc drmdefs drmopts)
set(RTKIT "${CMAKE_CURRENT_SOURCE_DIR}/rtkit")
add_fbsdkpi_driver(SFAppleCIO CLASS SFAppleCIO
 BUNDLE_IDENTIFIER org.siliconforest.driver.SFAppleCIO
 INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist" LINUXKPI_DRM
 SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/glue.c" "${CMAKE_CURRENT_SOURCE_DIR}/sf_cio_rtkit.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/sf_dart_table.c" "${CMAKE_CURRENT_SOURCE_DIR}/sf_apple_nhi.c" "${CMAKE_CURRENT_SOURCE_DIR}/sf_cio_tunables.c" "${CMAKE_CURRENT_SOURCE_DIR}/sf_pciec_config.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/sf_pciec_port.c" "${CMAKE_CURRENT_SOURCE_DIR}/sf_pciec_tunables.c"
  "${RTKIT}/mailbox.c" "${RTKIT}/rtkit.c" "${RTKIT}/rtkit-crashlog.c" "${RTKIT}/sf_rtkit_compat.c"
  "${SF_DRM_DIR}/src/sf_drm_printf.c"
 LKPI_INCLUDES ${drminc} LKPI_DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"linuxkpi\"
 LKPI_OPTIONS ${drmopts}
 DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"applecio\"
 INCLUDES "${RTKIT}/include" "${RTKIT}" "${CMAKE_CURRENT_SOURCE_DIR}" ${drminc}
 XNU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SFAppleCIO.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/SFCIOPCIHost.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SFCIOPCIController.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/sf_cio_dart.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sf_soc.cpp" "${RTKIT}/SFRtkitPlatform.cpp")
target_include_directories(SFAppleCIO.kext PRIVATE "${RTKIT}" "${CMAKE_CURRENT_SOURCE_DIR}")

# Optional actual native CM linkage. Its private devres implementation already
# belongs to this driver's layer; do not link a second parallel copy.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SFThunderboltCM/CMakeLists.txt")
 set(SF_TB_CM_FULL ON)
 set(SF_TB_CM_EMBEDDED ON)
 add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../SFThunderboltCM" "${CMAKE_CURRENT_BINARY_DIR}/cm")
 target_sources(SFAppleCIO.kext PRIVATE $<TARGET_OBJECTS:SFThunderboltCM> "${CMAKE_CURRENT_SOURCE_DIR}/SFCIOCM.cpp")
 target_compile_definitions(SFAppleCIO.kext PRIVATE SF_CIO_CM=1)
 target_link_options(SFAppleCIO.kext PRIVATE "LINKER:-dead_strip")
endif()

# fbsdkpi's global kernel headers already include IOPCIFamily. This build
# tree does not define the standalone platform's IOPCIFamilyHeaders target.
