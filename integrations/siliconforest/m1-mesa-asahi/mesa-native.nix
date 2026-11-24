# Native (x86_64-linux) build of the same Mesa source. It supplies two things:
#  - bin/mesa_clc, bin/vtn_bindgen2, bin/asahi_clc: the build-machine shader tools the cross build
#    needs (libagx is OpenCL C compiled at build time; this keeps LLVM out of the Darwin build);
#  - a Linux asahi driver with surfaceless EGL plus Mesa's own asahi drm-shim
#    (lib/libasahi_noop_drm_shim.so), used to run agx-offscreen-test on the builder with no GPU and
#    record the ioctl sequence (see trace/).  llvmpipe is included as a software sanity renderer.
let
  c = import ./common.nix;
  inherit (c) pkgs;
  clangLib = pkgs.lib.getLib pkgs.llvmPackages.clang-unwrapped;
in pkgs.stdenv.mkDerivation {
  pname = "darwin-lab-mesa-asahi-native";
  version = c.mesaVersion;
  src = c.mesaSrc;

  nativeBuildInputs = with pkgs; [
    meson ninja pkg-config c.pythonEnv bison flex glslang llvmPackages.libllvm.dev
  ];
  buildInputs = with pkgs; [
    llvmPackages.clang-unwrapped llvmPackages.libclc llvmPackages.libllvm
    spirv-llvm-translator spirv-tools libdrm zlib expat elfutils
  ];

  # clang's libraries and opencl headers live outside llvm's libdir in nixpkgs
  postPatch = ''
    patchShebangs .
    substituteInPlace meson.build \
      --replace-fail "dirs : llvm_libdir" "dirs : [llvm_libdir, '${clangLib}/lib']"
    substituteInPlace src/compiler/clc/meson.build \
      --replace-fail "join_paths(llvm_libdir, 'clang', clang_version_dir, 'include')" \
                     "join_paths('${clangLib}/lib', 'clang', clang_version_dir, 'include')"
  '';

  mesonBuildType = "release";
  mesonFlags = [
    "-Dauto_features=auto"
    "-Dgallium-drivers=asahi,llvmpipe,softpipe"
    "-Dvulkan-drivers="
    "-Dplatforms="
    "-Dopengl=true"
    "-Dgles1=disabled"
    "-Dgles2=enabled"
    "-Degl=enabled"
    "-Dglx=disabled"
    "-Dgbm=enabled"
    "-Dglvnd=disabled"
    "-Dllvm=enabled"
    "-Dshared-llvm=enabled"
    "-Dmesa-clc=enabled"
    "-Dinstall-mesa-clc=true"
    "-Dmesa-clc-bundle-headers=enabled"
    "-Dprecomp-compiler=enabled"
    "-Dinstall-precomp-compiler=true"
    "-Dtools=drm-shim"
    "-Dgallium-va=disabled"
    "-Dgallium-rusticl=false"
    "-Dvalgrind=disabled"
    "-Dlibunwind=disabled"
    "-Dlmsensors=disabled"
    "-Dzstd=disabled"
    "-Dbuild-tests=false"
  ];
  dontStrip = true;
}
