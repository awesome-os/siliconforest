# SPDX-License-Identifier: MIT
add_kext_bundle(SFGpuDT.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFGpuDT BUNDLE_VERSION 0.1.0)
set_property(TARGET SFGpuDT.kext PROPERTY OUTPUT_NAME SFGpuDT)
target_sources(SFGpuDT.kext PRIVATE SFGpuDT.cpp gpudt.c gpudt_summary.c SFDispDT.cpp dispdt.c)
target_include_directories(SFGpuDT.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SFGpuDT.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFGpuDT.kext PRIVATE -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFGpuDT.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFGpuDT.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
