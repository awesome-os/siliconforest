# SPDX-License-Identifier: GPL-2.0
add_kext_bundle(SFAppleTypeC.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFAppleTypeC BUNDLE_VERSION 0.1.0)
set_property(TARGET SFAppleTypeC.kext PROPERTY OUTPUT_NAME SFAppleTypeC)
target_sources(SFAppleTypeC.kext PRIVATE SFAppleTypeC.cpp sf_i2c.cpp sf_soc.cpp)
target_include_directories(SFAppleTypeC.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SFAppleTypeC.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFAppleTypeC.kext PRIVATE -Os -Wall -Wextra -Wno-inconsistent-missing-override)
set_property(TARGET SFAppleTypeC.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFAppleTypeC.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
