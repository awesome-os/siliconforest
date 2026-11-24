# SFBwfm: Broadcom BCM4378 (14e4:4425) PCIe FullMAC 802.11ax of the Apple M1 on IO80211Family.
# The radio is OpenBSD's bwfm(4), ISC licensed (see the file headers and OPENBSD_REVISION).
# Firmware is not built in, it loads from /Library/Firmware/brcm under the linux names.
add_kext_bundle(SFBwfm.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFBwfm BUNDLE_VERSION 1.0.0)
set_property(TARGET SFBwfm.kext PROPERTY OUTPUT_NAME SFBwfm)

set(SFBWFM_C_SOURCES bwfm.c if_bwfm_pci.c sf_bwfm_glue.c)
# sf_dart_map.cpp (per-buffer DART mapping, the default) and sf_dart_pool.cpp (the fixed bounce
# pool) are GPL-2.0, from ../m1-apcie-host/dart; they make this kext GPL-2.0 as a whole
target_sources(SFBwfm.kext PRIVATE ${SFBWFM_C_SOURCES} sf_dart_map.cpp sf_dart_pool.cpp SFBwfm.cpp)
# DMA switches, all off by default:
#   SF_BWFM_DMA_BOUNCE    the fixed bounce pool instead of per-buffer mapping
#   SF_BWFM_DMA_RX_SLOTS  per-buffer mapping, but receive goes through permanently mapped slots
#   SF_DART_STRICT        invalidate the DART TLB in every map and unmap, not once per ring commit
foreach(sf_bwfm_switch SF_BWFM_DMA_BOUNCE SF_BWFM_DMA_RX_SLOTS SF_DART_STRICT)
    option(${sf_bwfm_switch} "SFBwfm: see CMakeLists.txt" OFF)
    if(${sf_bwfm_switch})
        target_compile_definitions(SFBwfm.kext PRIVATE ${sf_bwfm_switch})
    endif()
endforeach()
set_source_files_properties(${SFBWFM_C_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/../IO80211Family/compat/sf80211_compat.h;-Wno-sign-compare;-Wno-unused-parameter;-Wno-unused-function;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-strict-prototypes;-Wno-missing-field-initializers;-Wno-shadow;-Wno-cast-qual;-Wno-pointer-sign;-Wno-visibility;-ferror-limit=0")

target_include_directories(SFBwfm.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IO80211Family
    ${CMAKE_CURRENT_SOURCE_DIR}/../IO80211Family/compat
    ${CMAKE_CURRENT_BINARY_DIR}/../IO80211Family/shim-include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOPCIFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../xnu/EXTERNAL_HEADERS)
target_link_libraries(SFBwfm.kext PRIVATE xnu_kernel_private_headers)
target_compile_definitions(SFBwfm.kext PRIVATE __PRIVATE_SPI__)
target_compile_options(SFBwfm.kext PRIVATE
    -Wno-sign-conversion -Wno-implicit-int-conversion -Wno-shorten-64-to-32)
set_property(TARGET SFBwfm.kext PROPERTY CXX_STANDARD 14)
add_dependencies(SFBwfm.kext IO80211Family.kext)
install(TARGETS SFBwfm.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
