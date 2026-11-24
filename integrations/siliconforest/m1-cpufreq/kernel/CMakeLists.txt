# SPDX-License-Identifier: GPL-2.0-only
add_kext_bundle(SFCpuPower.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFCpuPower BUNDLE_VERSION 0.1.0)
set_property(TARGET SFCpuPower.kext PROPERTY OUTPUT_NAME SFCpuPower)
target_sources(SFCpuPower.kext PRIVATE SFCpuPower.cpp SFSmcSession.cpp sf_cpufreq_policy.c sf_thermal_policy.c)
target_include_directories(SFCpuPower.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SFCpuPower.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFCpuPower.kext PRIVATE -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFCpuPower.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFCpuPower.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
