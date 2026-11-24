add_kext_bundle(PDM1SPIHID.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.puredarwin.driver.PDM1SPIHID BUNDLE_VERSION 0.1.0
    ANTIMAIN_FUNCTION pd_m1kbd_stop)
set_property(TARGET PDM1SPIHID.kext PROPERTY OUTPUT_NAME PDM1SPIHID)
target_sources(PDM1SPIHID.kext PRIVATE
    PDM1SPIHID.cpp PDM1KeyboardHW.cpp m1kbd_board.c m1kbd_pio.c m1kbd_hid.c
    native_protocol/dl_spi_hid.c)
target_include_directories(PDM1SPIHID.kext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDSystem
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/IOHIDFamily
    ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily)
target_link_libraries(PDM1SPIHID.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(PDM1SPIHID.kext PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/../IOHIDFamily/include/pd_hid_compat.h>"
    -Wall -Wextra -Wframe-larger-than=1024)
set_property(TARGET PDM1SPIHID.kext PROPERTY C_STANDARD 11)
set_property(TARGET PDM1SPIHID.kext PROPERTY CXX_STANDARD 14)
install(TARGETS PDM1SPIHID.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
