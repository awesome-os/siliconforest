# SPDX-License-Identifier: GPL-2.0-only
add_kext_bundle(SFAppleAVD.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFAppleAVD BUNDLE_VERSION 0.1.0)
set_property(TARGET SFAppleAVD.kext PROPERTY OUTPUT_NAME SFAppleAVD)
target_sources(SFAppleAVD.kext PRIVATE SFAppleAVD.cpp sf_avd_dart.cpp sf_soc.cpp sf_avd_core.c
    linux/avd-hw.c linux/avd-h264.c linux/avd-hevc.c linux/avd-vp9.c linux/v4l2-h264.c linux/v4l2-vp9.c)
target_include_directories(SFAppleAVD.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/linux)
target_link_libraries(SFAppleAVD.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFAppleAVD.kext PRIVATE -Wall -Wno-inconsistent-missing-override -Wno-unused-function
    "$<$<COMPILE_LANGUAGE:C>:-std=gnu11>" "$<$<COMPILE_LANGUAGE:C>:-Os>")
set_property(TARGET SFAppleAVD.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFAppleAVD.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
