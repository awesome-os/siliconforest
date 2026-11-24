# Runs the Rust side of SFRustHello on the build host (x86_64 Linux, the same rustc and
# sources, libc behind the kernel imports; see host-check/host.c) and fails unless the
# value in kext/sf_rust_hello.h (SF_RUST_HELLO_EXPECT) is what the code returns.
# $out/log is the text the kext is expected to log. Not evidence about the arm64 build.
let
  inherit (import ./pins.nix) pkgs;
in pkgs.stdenv.mkDerivation {
  name = "sfrusthello-host-check";
  dontUnpack = true;
  nativeBuildInputs = [ pkgs.rustc.unwrapped ];
  buildPhase = ''
    R="rustc --edition=2024 -Copt-level=2 -Cpanic=abort -Ccodegen-units=1"
    $R --crate-type rlib --crate-name sf_xnu ${./rust/sf_xnu}/lib.rs \
      --remap-path-prefix=${./rust/sf_xnu}=sf_xnu
    $R --crate-type staticlib --crate-name sfrusthello ${./rust/sfrusthello}/lib.rs \
      --remap-path-prefix=${./rust/sfrusthello}=sfrusthello --extern sf_xnu=libsf_xnu.rlib
    cc -O1 -Wall -Werror -I${./kext} ${./host-check/host.c} libsfrusthello.a -o host
    mkdir -p $out
    ./host | tee $out/log
  '';
  dontInstall = true;
}
