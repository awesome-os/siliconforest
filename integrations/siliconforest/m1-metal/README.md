# m1-metal: the Metal experiment (Darling's Metal stack on our Vulkan)

An experiment agreed with the owner: Darling's Metal implementation on SiliconForest, on the M1's GPU
through Honeykrisp (`../m1-vulkan`). Results, what works and what Iridium cannot translate:
`docs/roadmap/issues/SF-AE-metal.md`. Licences: `NOTICE` (Indium/Iridium 0BSD, darling-metal MPL-2.0,
mslc MIT; nothing from the GPL-3.0 main Darling repository).

| Recipe | What |
|---|---|
| `common.nix` | pinned sources (fetched, never copied here), the arm64 Darwin toolchain of `../m1-vulkan`, LLVM 21 (the SiliconForest flake's `llvm-arm64`) |
| `indium.nix` | libindium, libiridium, libindium_kit and Indium's own window-less tests (basic-compute with its `add.metallib`, basic-compute-mslc, device-info), arm64 Darwin; `patches/indium/0001`: the Vulkan loader by its Mach-O name |
| `mslc.nix` | libmslc.dylib (arm64 Darwin, compiler-rt builtins linked in for the 128-bit conversions) and `mslc-host` on the workstation |
| `metal.nix` | darling-metal's Metal.framework (arm64 Darwin, against SiliconForest's libobjc/Foundation/CoreFoundation; our header shims in `shim/`), and `sf-metal-tri`, our own test program (`test/sf-metal-tri.m`, shaders `test/sf-tri.metal`) |
| `stage.nix` | the tree for `/private/var/db/sfmetal` on the M1 (libLLVM.dylib included: Iridium dlopens it by full path) |
| `host.nix` | Indium's own CMake build on the workstation: `mtl2spv`, to see what Iridium makes of a metallib |

```
export PATH=$HOME/.nix-profile/bin:$PATH
tools/sfmetal.sh push                                  # stage -> /private/var/db/sfmetal (sfmetal.prev kept)
tools/sfmetal.sh careful <name> './bin/indium-test-basic-compute; ./bin/sf-metal-tri'
```
`careful` runs through `../m1-vulkan/tools/sfvk-careful.sh` (gui.lock, UART on the M5, kernel log
streamed). The programs find Vulkan as any program does (the system install of `../m1-vulkan/system.nix`).
Nothing of the installed system is replaced; `rm -rf /private/var/db/sfmetal*` removes it all.

Shaders: only the metallibs of Indium's own test suite are used (Apple-toolchain output that ships in
that 0BSD project), and our MSL compiled by mslc. No metallib from an Apple application is used.
