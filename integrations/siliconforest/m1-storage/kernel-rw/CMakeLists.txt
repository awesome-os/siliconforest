# SPDX-License-Identifier: GPL-2.0-only
# SFAppleNVMeRW: the WRITE-FENCED build of the internal NVMe driver. Its sources are ../kernel
# with rw.patch applied (see ../tools/rw_src.sh) plus sf_nvme_wgate.c and sf_nvme_wring.c.
# Never in a default collection.
add_kext_bundle(SFAppleNVMeRW.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFAppleNVMeRW BUNDLE_VERSION 0.1.0)
set_property(TARGET SFAppleNVMeRW.kext PROPERTY OUTPUT_NAME SFAppleNVMeRW)
target_sources(SFAppleNVMeRW.kext PRIVATE SFAppleNVMe.cpp sf_ans.cpp sf_nvme_gate.c sf_nvme_wgate.c sf_nvme_wring.c sf_nvme_part.c sf_soc.cpp)
target_include_directories(SFAppleNVMeRW.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOStorageFamily/include)
target_link_libraries(SFAppleNVMeRW.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(SFAppleNVMeRW.kext PRIVATE -Wall -Wno-inconsistent-missing-override -Os)
set_property(TARGET SFAppleNVMeRW.kext PROPERTY CXX_STANDARD 14)
install(TARGETS SFAppleNVMeRW.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
