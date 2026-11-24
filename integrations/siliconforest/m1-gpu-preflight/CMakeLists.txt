add_kext_bundle(PDGPUPreflight.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.puredarwin.driver.PDGPUPreflight BUNDLE_VERSION 0.1.0
    ANTIMAIN_FUNCTION pd_gpu_preflight_stop)
set_property(TARGET PDGPUPreflight.kext PROPERTY OUTPUT_NAME PDGPUPreflight)
target_sources(PDGPUPreflight.kext PRIVATE PDGPUPreflight.cpp board.c observe.c)
target_include_directories(PDGPUPreflight.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PDGPUPreflight.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(PDGPUPreflight.kext PRIVATE -Wall -Wextra -Wframe-larger-than=1024)
set_property(TARGET PDGPUPreflight.kext PROPERTY C_STANDARD 11)
set_property(TARGET PDGPUPreflight.kext PROPERTY CXX_STANDARD 14)
install(TARGETS PDGPUPreflight.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
