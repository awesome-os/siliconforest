# SPDX-License-Identifier: GPL-2.0-only
add_kext_bundle(SFAppleTouchBar.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFAppleTouchBar BUNDLE_VERSION 0.1.0)
set_property(TARGET SFAppleTouchBar.kext PROPERTY OUTPUT_NAME SFAppleTouchBar)
target_sources(SFAppleTouchBar.kext PRIVATE SFAppleTouchBar.cpp sf_soc.cpp sf_gpio.cpp sf_dart_fixed.cpp
    sf_adp.cpp sf_spi.cpp sf_z2.cpp sf_tb_keys.c)
target_include_directories(SFAppleTouchBar.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDSystem
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDFamily
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily)
target_link_libraries(SFAppleTouchBar.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFAppleTouchBar.kext PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/include/sf_hid_compat.h>"
    -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFAppleTouchBar.kext PROPERTY C_STANDARD 11)
set_property(TARGET SFAppleTouchBar.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFAppleTouchBar.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
