# SPDX-License-Identifier: GPL-2.0
# Object library for the upcoming Apple DWC3/xHCI integration. It is not an
# attached kext or a success stub for the missing host-controller lifecycle.
add_library(SFUSBDma OBJECT sf_usb_dart.cpp sf_dart_table.c sf_soc.cpp)
target_include_directories(SFUSBDma PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SFUSBDma PRIVATE xnu_kernel_private_headers)
target_compile_options(SFUSBDma PRIVATE -Os -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFUSBDma PROPERTY CXX_STANDARD 14)
