# SPDX-License-Identifier: GPL-2.0-only
add_kext_bundle(SFAppleNVMe.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFAppleNVMe BUNDLE_VERSION 0.1.0)
set_property(TARGET SFAppleNVMe.kext PROPERTY OUTPUT_NAME SFAppleNVMe)
target_sources(SFAppleNVMe.kext PRIVATE SFAppleNVMe.cpp sf_ans.cpp sf_nvme_gate.c sf_nvme_part.c sf_soc.cpp)
target_include_directories(SFAppleNVMe.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include)
target_link_libraries(SFAppleNVMe.kext PRIVATE xnu_kernel_private_headers)
# -Os: the collection has 0x2000 bytes to spare before the kernel image, and members are padded to 16 KiB per segment
target_compile_options(SFAppleNVMe.kext PRIVATE -Wall -Wno-inconsistent-missing-override -Os)
set_property(TARGET SFAppleNVMe.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFAppleNVMe.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
