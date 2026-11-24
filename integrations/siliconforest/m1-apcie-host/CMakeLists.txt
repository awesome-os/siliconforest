# SPDX-License-Identifier: GPL-2.0
add_kext_bundle(SFApcieT8103Bridge.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFApcieT8103Bridge BUNDLE_VERSION 0.2.0)
set_property(TARGET SFApcieT8103Bridge.kext PROPERTY OUTPUT_NAME SFApcieT8103Bridge)
target_sources(SFApcieT8103Bridge.kext PRIVATE SFApcieT8103.cpp SFApcieT8103Link.cpp SFAppleSMC.cpp)
target_include_directories(SFApcieT8103Bridge.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SFApcieT8103Bridge.kext PRIVATE xnu_kernel_private_headers IOPCIFamilyHeaders)
target_compile_options(SFApcieT8103Bridge.kext PRIVATE -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFApcieT8103Bridge.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFApcieT8103Bridge.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
