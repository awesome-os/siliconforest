add_kext_bundle(PDApcieT8103Bridge.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.puredarwin.driver.PDApcieT8103Bridge BUNDLE_VERSION 0.1.0
    ANTIMAIN_FUNCTION pd_apcie_bridge_stop)
set_property(TARGET PDApcieT8103Bridge.kext PROPERTY OUTPUT_NAME PDApcieT8103Bridge)
# board.c and survey.h are the frozen m1-dart-survey files, copied unchanged by the recipe.
target_sources(PDApcieT8103Bridge.kext PRIVATE PDApcieT8103Bridge.cpp board.c observe.c)
target_include_directories(PDApcieT8103Bridge.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PDApcieT8103Bridge.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(PDApcieT8103Bridge.kext PRIVATE -Wall -Wextra -Wframe-larger-than=1024)
set_property(TARGET PDApcieT8103Bridge.kext PROPERTY C_STANDARD 11)
set_property(TARGET PDApcieT8103Bridge.kext PROPERTY CXX_STANDARD 14)
install(TARGETS PDApcieT8103Bridge.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
