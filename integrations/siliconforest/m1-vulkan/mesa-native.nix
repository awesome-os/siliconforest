# Native (x86_64-linux) build of Mesa b7b03c5 (./common.nix), as ../m1-mesa-asahi/mesa-native.nix is
# for 147a62f, plus the Vulkan drivers asahi (Honeykrisp) and swrast (lavapipe). It supplies:
#  - bin/mesa_clc, bin/vtn_bindgen2, bin/asahi_clc: the build-machine shader tools the cross build
#    needs (libagx is OpenCL C compiled at build time; this keeps LLVM out of the Darwin build);
#  - Honeykrisp and the gallium asahi driver for Linux on Mesa's asahi drm-shim
#    (lib/libasahi_noop_drm_shim.so): the Vulkan tests run on the builder with no GPU behind them;
#  - lavapipe, the software Vulkan reference for the triangle hash (README.md in this directory).
# Vulkan wants wayland-scanner and libwayland for its WSI only when platforms are set; none are.
let
  c = import ./common.nix;
  inherit (c) pkgs;
  clangLib = pkgs.lib.getLib pkgs.llvmPackages.clang-unwrapped;
in pkgs.stdenv.mkDerivation {
  pname = "darwin-lab-mesa-vk-native";
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
    "-Dvulkan-drivers=asahi,swrast"
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
