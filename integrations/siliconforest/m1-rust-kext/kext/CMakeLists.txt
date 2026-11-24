# SPDX-License-Identifier: GPL-2.0
# SFRustHello: a C++ IOKit shell plus one prelinked Rust object (rust/sfrusthello.o, put
# here by ../build-kext.nix from ../build-rust.nix). The Rust side is not compiled by
# CMake: its target and flags live in ../rust/toolchain.nix.
add_kext_bundle(SFRustHello.kext INFO_PLIST Info.plist
    BUNDLE_IDENTIFIER org.siliconforest.driver.SFRustHello BUNDLE_VERSION 0.1.0)
set_property(TARGET SFRustHello.kext PROPERTY OUTPUT_NAME SFRustHello)
target_sources(SFRustHello.kext PRIVATE SFRustHello.cpp)
target_include_directories(SFRustHello.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(SFRustHello.kext PRIVATE -Wall -Wno-inconsistent-missing-override)
set_property(TARGET SFRustHello.kext PROPERTY CXX_STANDARD 14)

set(SFRUSTHELLO_RUST_OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/rust/sfrusthello.o)
if(NOT EXISTS ${SFRUSTHELLO_RUST_OBJECT})
    message(FATAL_ERROR "SFRustHello: ${SFRUSTHELLO_RUST_OBJECT} is missing (build-rust.nix output)")
endif()
set_source_files_properties(${SFRUSTHELLO_RUST_OBJECT} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED FALSE)
target_sources(SFRustHello.kext PRIVATE ${SFRUSTHELLO_RUST_OBJECT})
# core and alloc are linked whole into that object; drop what nothing reaches. Roots are the
# kext's global symbols (the C++ side, kmod_info), so only unreferenced Rust code goes.
target_link_options(SFRustHello.kext PRIVATE "LINKER:-dead_strip")

install(TARGETS SFRustHello.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
