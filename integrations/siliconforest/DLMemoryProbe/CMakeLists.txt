# Experimental, manually staged into a separate PureDarwin checkout.
# Source-reviewed recipe, NOT a successful kernel-build result.
add_kext_bundle(DLMemoryProbe.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.darwinlab.driver.DLMemoryProbe BUNDLE_VERSION 0.0.1
    MAIN_FUNCTION dlmp_start ANTIMAIN_FUNCTION dlmp_stop)
set_property(TARGET DLMemoryProbe.kext PROPERTY OUTPUT_NAME DLMemoryProbe)
set_property(TARGET DLMemoryProbe.kext PROPERTY C_STANDARD 11)
target_sources(DLMemoryProbe.kext PRIVATE
    DLMemoryProbe.cpp provider_iokit.cpp
    lab/contracts/buffer_lifetime.c lab/core/backing_store.c
    lab/core/mapping_ticket.c lab/probe/memory_probe.c)
target_include_directories(DLMemoryProbe.kext PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../fbsdkpi"
    "${CMAKE_CURRENT_SOURCE_DIR}/lab/contracts"
    "${CMAKE_CURRENT_SOURCE_DIR}/lab/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/lab/probe")
target_link_libraries(DLMemoryProbe.kext PRIVATE xnu_kernel_private_headers)
install(TARGETS DLMemoryProbe.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
# Installing a bundle does NOT prove it is selected for or started from the KC.
# Verify collection inclusion/exports and activation on the pinned build first.
