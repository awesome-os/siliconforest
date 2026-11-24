# SPDX-License-Identifier: GPL-2.0-only OR MIT
# SFDcp: Omacom's display-coprocessor driver (drivers/gpu/drm/apple at linux-aurora 6a08a08e)
# as a LinuxKPI DRM kext. In the bundle:
#   - the layer as every LinuxKPI DRM kext links it, hidden visibility
#   - the DRM core at Linux 7.2 (m1-drm72)
#   - driver/: the driver's sources with ../patches applied (build-kext.nix stages them)
#   - pin/: DRM helpers and the component framework the core lacks, from the pin as they are
#   - pininc/: headers from the pin (tools/pin-headers.txt)
#   - rtkit/: the Apple mailbox/RTKit port (m1-gpu-rtkit/src); dart/: m1-dcp-dart
#   - the tree's IOKit class for a device-tree platform device, attaching with sfdcp=1|2
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/fbsdkpi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi/drm/drm.cmake")

set(SFDCP_C_OPT -Os)
set(drmkmod "${SF_DRMKMOD_DIR}")
sf_drm_layout("${drmkmod}" drminc drmdefs drmopts)
sf_drm_core_includes("${drmkmod}" coreinc)
set(HERE "${CMAKE_CURRENT_SOURCE_DIR}")
set(RTKIT "${HERE}/rtkit")
set(DART "${HERE}/dart")

set(lkpiinc ${drminc}
    "${FBSDKPI_DIR}/sf/lkpi-override"
    "${FBSDKPI_DIR}/freebsd/linuxkpi/common/include"
    "${FBSDKPI_DIR}/freebsd/linuxkpi/dummy/include"
    "${FBSDKPI_DIR}/sf/fbsd-extra"
    "${FBSDKPI_DIR}/freebsd"
    "${FBSDKPI_DIR}/haiku/freebsd_iflib/compat")
set(lkpiopt "SHELL:-include ${FBSDKPI_DIR}/sf/fbsd-extra/sf_lkpi_pre.h"
    ${drmopts} -Werror=implicit-function-declaration)

# Space (one collection has to hold SFAgx and SFDcp, each with its own DRM core): TTM and the
# DisplayPort/HDMI/DSC helper library are for drivers that use them; neither this driver nor
# the DRM core itself references them. SF_DRM_KEEP_ALL=ON builds the full list again.
option(SF_DRM_KEEP_ALL "Build TTM and the display helper library into the DRM core" OFF)
if(NOT SF_DRM_KEEP_ALL)
    list(FILTER SF_DRM_CORE_SOURCES EXCLUDE REGEX "^(ttm|display)/")
endif()
set(core)
foreach(src IN LISTS SF_DRM_CORE_SOURCES)
    list(APPEND core "${drmkmod}/drivers/gpu/drm/${src}")
endforeach()
foreach(src IN LISTS SF_DRM_DMABUF_SOURCES)
    list(APPEND core "${drmkmod}/drivers/dma-buf/${src}")
endforeach()
foreach(src IN LISTS SF_DRM_VIDEO_SOURCES)
    list(APPEND core "${drmkmod}/drivers/video/${src}")
endforeach()
foreach(src IN LISTS SF_DRM_PD_SOURCES)
    list(APPEND core "${SF_DRM_DIR}/src/${src}")
endforeach()
_fbsdkpi_objects(SFDcp_drm SOURCES ${core}
    INCLUDES ${lkpiinc} ${coreinc}
    DEFINITIONS FBSD_DRIVER=1 _XOPEN_SOURCE ${drmdefs}
        KBUILD_MODNAME=\"drm\" LINUXKPI_PARAM_PREFIX=drm_
    OPTIONS ${lkpiopt} ${SFDCP_C_OPT})

# the driver: the 27 files J293's internal panel can reach or that only cost space to keep.
# Not built: dcpext_scanout.c, dcpext_drm.c, j613_dcp_activate.c (sf_dcp_linux.c, part 3),
# audio.c, av.c (CONFIG_DRM_APPLE_AUDIO off) and the tests.
set(drv)
foreach(f apple_drv apple_resume afk dcp dcp-fabric dcp-fabric-core dcp_backlight dptxep iomfb parser systemep
        dcp-hdmi connector dcp-display-metadata dcp-180hz dcp-panel-edid ibootep
        iomfb_v12_3 iomfb_v13_3 iomfb_v14_7_tpl iomfb_v14_7 iomfb_v14_7_link iomfb_h17p
        iomfb_v26_6 iomfb_v26_6_link epic/dpavservep plane)
    list(APPEND drv "${HERE}/driver/${f}.c")
endforeach()
set(pin "${HERE}/pin/drm_gem_dma_helper.c" "${HERE}/pin/drm_fb_dma_helper.c"
    "${HERE}/pin/drm_gem_framebuffer_helper.c" "${HERE}/pin/drm_simple_kms_helper.c"
    "${HERE}/pin/component.c")
set(dcpopt ${lkpiopt}
    "SHELL:-include ${HERE}/include/sf_dcp_compat.h"
    "SHELL:-include ${DART}/include/sf_dcp_iommu.h" ${SFDCP_C_OPT} -Wno-everything
    -Werror=implicit-function-declaration)
# the layer's sf_drm_gem_helpers.c already has three functions of this file; the pin's copies
# keep to the file under other names
set_source_files_properties("${HERE}/pin/drm_gem_framebuffer_helper.c" PROPERTIES COMPILE_DEFINITIONS
    "drm_gem_fb_get_obj=sf_pin_drm_gem_fb_get_obj;drm_gem_fb_destroy=sf_pin_drm_gem_fb_destroy;drm_gem_fb_create_handle=sf_pin_drm_gem_fb_create_handle")
# the 6.14 core calls mode_config_funcs.fb_create with three arguments (kext/sf_dcp_mmap.c)
set_source_files_properties("${HERE}/driver/apple_drv.c" PROPERTIES COMPILE_DEFINITIONS
    "drm_gem_fb_create=sf_dcp_gem_fb_create")
# The RTKit receive queue is WQ_HIGHPRI on linux. m1-gpu-rtkit's compat header creates it
# with linux_create_workqueue_common(), dropping the flag; here that one call makes the
# layer's high-priority queue (layer patch 0025, boot-arg sflkpi_wq_highpri=0 for the old way).
set_source_files_properties("${RTKIT}/rtkit.c" PROPERTIES
    COMPILE_DEFINITIONS "linux_create_workqueue_common=linux_create_workqueue_highpri")
_fbsdkpi_objects(SFDcp_drv SOURCES ${drv} ${pin}
        "${HERE}/sf_dcp_linux.c" "${HERE}/sf_dcp_mmap.c" "${HERE}/sf_dcp_prime.c" "${HERE}/sf_dcp_backlight.c" "${HERE}/sf_dcp_hotplug.c"
        "${HERE}/sf_dcp_tunnel_bridge.c"
        "${DART}/src/sf_dcp_iommu.c" "${DART}/src/sf_dart_table.c"
        "${RTKIT}/mailbox.c" "${RTKIT}/rtkit.c" "${RTKIT}/rtkit-crashlog.c" "${RTKIT}/sf_rtkit_compat.c"
    INCLUDES "${HERE}/pininc" "${HERE}/driver" "${HERE}/include" "${DART}/include"
        "${RTKIT}/include" "${RTKIT}" ${lkpiinc} ${coreinc}
    DEFINITIONS FBSD_DRIVER=1 _XOPEN_SOURCE ${drmdefs} CONFIG_MMU
        KBUILD_MODNAME=\"appledrm\" LINUXKPI_PARAM_PREFIX=appledrm_
    OPTIONS ${dcpopt})
# the 7.2 atomic names: compiled without the compat header's rename (see the file)
_fbsdkpi_objects(SFDcp_drm72 SOURCES "${HERE}/sf_dcp_drm72.c"
    INCLUDES "${HERE}/pininc" ${lkpiinc} ${coreinc}
    DEFINITIONS FBSD_DRIVER=1 _XOPEN_SOURCE ${drmdefs}
        KBUILD_MODNAME=\"appledrm\" LINUXKPI_PARAM_PREFIX=appledrm72_
    OPTIONS ${lkpiopt} ${SFDCP_C_OPT})

add_fbsdkpi_driver(SFDcp
    CLASS SFDcp
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFDcp
    INFO_PLIST "${HERE}/Info.plist"
    LINUXKPI_DRM
    SOURCES "${HERE}/glue.c"
    LKPI_INCLUDES ${drminc}
    LKPI_DEFINITIONS ${drmdefs} KBUILD_MODNAME=\"linuxkpi\"
    LKPI_OPTIONS ${drmopts}
    OBJECTS SFDcp_drm SFDcp_drv SFDcp_drm72
    XNU_SOURCES "${HERE}/../../fbsdkpi/xnu/SFDRMPlatformDevice.cpp"
        "${RTKIT}/SFRtkitPlatform.cpp"
        "${DART}/src/sf_dart.cpp"
        "${HERE}/SFDcpHost.cpp" "${HERE}/SFDcpSysctl.cpp"
)
# Space: the layer's own objects are compiled without -O by the tree. In this kext they get
# -Os too (SFAgx keeps the tree's setting), which is what lets SFAgx and SFDcp share one
# collection. SFDCP_LAYER_O0=ON builds them as the tree does.
option(SFDCP_LAYER_O0 "Compile the layer objects of SFDcp without optimisation" OFF)
if(NOT SFDCP_LAYER_O0)
    foreach(t SFDcp_core SFDcp_lkpi SFDcp_lkpiglue SFDcp_driver SFDcp_iflib)
        if(TARGET ${t})
            target_compile_options(${t} PRIVATE -Os)
        endif()
    endforeach()
endif()

target_compile_definitions(SFDcp.kext PRIVATE SFDRM_OPT_IN_BOOTARG="sfdcp"
    SFDRMFramebuffer=SFDRMFramebufferDcp SFDRMPresenter=SFDRMPresenterDcp)
target_include_directories(SFDcp.kext PRIVATE "${RTKIT}" "${DART}/include"
    "${HERE}/../IOGraphicsFamily" "${HERE}/../../fbsdkpi/drm/include")
target_link_options(SFDcp.kext PRIVATE -Wl,-x -Wl,-map,${CMAKE_BINARY_DIR}/SFDcp.map)
