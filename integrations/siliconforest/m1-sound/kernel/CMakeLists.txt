# SPDX-License-Identifier: GPL-2.0-only
add_kext_bundle(SFAppleSound.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFAppleSound BUNDLE_VERSION 0.1.0)
set_property(TARGET SFAppleSound.kext PROPERTY OUTPUT_NAME SFAppleSound)
target_sources(SFAppleSound.kext PRIVATE SFAppleSound.cpp sf_soc.cpp sf_gpio.cpp sf_i2c.cpp sf_dart_fixed.cpp
    sf_mca.cpp sf_cs42l83.cpp sf_tas2770.cpp sf_speakers.cpp sf_speaker_model.c)
target_include_directories(SFAppleSound.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SFAppleSound.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFAppleSound.kext PRIVATE -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFAppleSound.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFAppleSound.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
