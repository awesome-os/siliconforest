# The Rust side of SFRustHello.kext, in the three steps rust/toolchain.nix defines:
#   sysroot  core + compiler_builtins + alloc for aarch64-apple-xnukext (built once)
#   crates   sf_xnu (rlib: allocator, logger, panic handler) and sfrusthello (staticlib)
#   object   sfrusthello.o, the staticlib prelinked with only the C entry points exported
# Phase B adds its crates (bindings, kernel, drivers) as further mkCrate calls here.
#   nix-build build-rust.nix -A object     (also: -A sysroot, -A sfrusthello)
let
  pins = import ./pins.nix;
  rust = import ./rust/toolchain.nix { inherit (pins) pd pkgs crossToolchain; };
in rec {
  inherit rust;
  inherit (rust) sysroot;
  sf_xnu = rust.mkCrate { crateName = "sf_xnu"; src = ./rust/sf_xnu; };
  sfrusthello = rust.mkCrate {
    crateName = "sfrusthello";
    crateType = "staticlib";
    src = ./rust/sfrusthello;
    deps = [ sf_xnu ];
  };
  object = rust.mkKextObject {
    name = "sfrusthello";
    staticlib = sfrusthello;
    # the functions declared in kext/sf_rust_hello.h; everything else becomes local
    exports = [ "_sf_rust_hello_entry" "_sf_rust_hello_calls" ];
  };
}
