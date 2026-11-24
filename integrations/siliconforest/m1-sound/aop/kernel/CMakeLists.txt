# SPDX-License-Identifier: GPL-2.0-only
add_kext_bundle(SFAopAudio.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFAopAudio BUNDLE_VERSION 0.1.0)
set_property(TARGET SFAopAudio.kext PROPERTY OUTPUT_NAME SFAopAudio)
target_sources(SFAopAudio.kext PRIVATE SFAopAudio.cpp sf_aop.cpp sf_admac.cpp sf_soc.cpp sf_dart_fixed.cpp)
target_include_directories(SFAopAudio.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SFAopAudio.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFAopAudio.kext PRIVATE -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFAopAudio.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFAopAudio.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
