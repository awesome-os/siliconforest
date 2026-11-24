# Mesa (omacom integration/aurora 147a62f6190) for the SiliconForest M1 guest: arm64 Mach-O, Gallium
# asahi + softpipe, EGL (surfaceless only) and GLES2/3 on SiliconForest's /dev/dri libdrm.  No LLVM,
# X11, Wayland or GBM in the closure.  libagx's OpenCL C is compiled by the native build's tools.
#
#   nix-build ... mesa-darwin.nix (no window system) / mesa-darwin-wayland.nix
# `vulkan`, `c`, `nativeTools` and `extraPatches` are for ../m1-vulkan (another Mesa source and its
# host tools, Honeykrisp added). Since 14 November every build also carries patch 0011 (Mesa's
# monotonic clock is the kernel's) and ETIME=ETIMEDOUT (sf-etime.h): both GL derivations changed
# then; the GL Mesa installed on the M1 is the one built before (../m1-vulkan/README.md).
{ wayland ? false, vulkan ? false, c ? import ./common.nix, nativeTools ? import ./mesa-native.nix
, extraPatches ? [ ] }:
let
  inherit (c) pkgs p triple sdkRoot;
  lib = pkgs.lib;
  libdrm = p.libdrm-dri-arm64;
  zlib = p.zlib-arm64;
  expat = p.expat-arm64;
  tc = p.arm64-cross-toolchain;
  libSystem = p.libsystem-arm64;
  cxx = p.libcxxDylib-arm64;
  cxxabi = p.libcxxabiDylib-arm64;
  cf = p.coreFoundation-arm64;
  iokit = p.iokit-arm64;
  # Wayland EGL platform (client side): the flake's Darwin libwayland and wayland-protocols,
  # nixpkgs' wayland-scanner on the build machine
  wl = p.wayland-arm64;
  wlProtocols = p.wayland-protocols;
  ffi = p.libffi-arm64;
  wlPc = "${wl}/lib/pkgconfig:${wlProtocols}/share/pkgconfig:${ffi}/lib/pkgconfig:${ffi}/usr/lib/pkgconfig";
  wlEnv = lib.optionalString wayland (
    "\nexport PKG_CONFIG_PATH=\"\$PKG_CONFIG_PATH:${wlPc}\"" +
    "\nexport PKG_CONFIG_LIBDIR=\"\$PKG_CONFIG_PATH\"" +
    "\nexport PKG_CONFIG_PATH_FOR_BUILD=\"${lib.getDev pkgs.wayland-scanner}/lib/pkgconfig\"" +
    "\nexport PKG_CONFIG_LIBDIR_FOR_BUILD=\"\$PKG_CONFIG_PATH_FOR_BUILD\"");

  # every translation unit sees ETIME as ETIMEDOUT (sf-etime.h, made in postPatch)
  etimeArg = ", '-include', '$PWD/sf-compat/sf-etime.h'";
  q = s: "'${s}'";
  list = xs: lib.concatMapStringsSep ", " q xs;
  common = [ "-isysroot" sdkRoot "-mmacosx-version-min=26.5" ];
  cflags = common ++ [
    "-Qunused-arguments" "-U_FORTIFY_SOURCE" "-D_FORTIFY_SOURCE=0" "-fno-stack-protector"
    "-I${libSystem}/usr/include"
    "-I${libdrm}/include" "-I${libdrm}/include/libdrm"
    "-I${lib.getDev zlib}/include" "-I${lib.getDev expat}/include"
  ] ++ lib.optional wayland "-I${wl}/include";
  ldflags = common ++ [
    "-fuse-ld=${p.native-ld}/bin/ld" "-nostdlib" "-L${libSystem}/usr/lib"
    "-L${libdrm}/lib" "-L${zlib}/lib" "-L${expat}/lib"
    "-L${cf}/usr/lib" "-L${iokit}/usr/lib" "-lIOKitCF" "-lCoreFoundation"
    "-Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib"
    "-Wl,-platform_version,macos,26.5,26.5" "-Wl,-fixup_chains"
  ] ++ lib.optional wayland "-L${wl}/lib";
in pkgs.stdenv.mkDerivation ({
  pname = "darwin-lab-mesa-asahi${lib.optionalString vulkan "-vk"}${lib.optionalString wayland "-wayland"}-arm64-darwin";
  version = c.mesaVersion;
  src = c.mesaSrc;

  nativeBuildInputs = with pkgs; [ meson ninja pkg-config c.pythonEnv bison flex glslang ]
    ++ lib.optional wayland pkgs.wayland-scanner;

  patches = [
    ./patches/0001-meson-darwin-is-a-drm-system.patch
    ./patches/0002-asahi-no-macos-iokit-wrap-tooling.patch
    ./patches/0003-os-file-same-file-via-F_SF_SAMEFILE.patch
    ./patches/0004-u-screen-query-DRM_CAP_PRIME-on-darwin.patch
    ./patches/0005-asahi-ioc-field-accessors-for-bsd-ioccom.patch
    ./patches/0011-c11-monotonic-time-is-the-kernels-uptime-on-darwin.patch
  ] ++ lib.optionals wayland [
    ./patches/0006-egl-wayland-open-render-node-without-feedback.patch
    ./patches/0007-loader-wayland-ppoll-for-darwin.patch
    ./patches/0008-egl-pointer-check-without-mincore-on-darwin.patch
    ./patches/0009-report-why-a-window-buffer-could-not-be-exported.patch
    ./patches/0010-asahi-export-wait-for-writer-when-fence-import-fails.patch
  ] ++ extraPatches;

  postPatch = ''
    patchShebangs .
    # Darwin has no <sys/sysmacros.h>; major()/minor()/makedev() come from <sys/types.h>.  The
    # values are XNU's: 8-bit major, 24-bit minor (SFDRMDri puts the open slot above the low byte).
    mkdir -p sf-compat/sys
    cp ${./compat/sys/sysmacros.h} sf-compat/sys/sysmacros.h
    # The kernel's LinuxKPI turns Linux's ETIME into ETIMEDOUT (60), as FreeBSD's does; Darwin's
    # <errno.h> has a STREAMS ETIME (101) that no kernel path returns. Mesa compares DRM wait
    # results with ETIME (vk_drm_syncobj.c, wsi_common_drm.c, hk_query_pool.c, agx_fence.c, ...):
    # a syncobj wait that times out became VK_ERROR_UNKNOWN on the M1 (vk-tri, first fence wait).
    # Mesa uses no STREAMS, so ETIME means the DRM timeout everywhere in it.
    printf '%s\n' '#include <errno.h>' '#undef ETIME' '#define ETIME ETIMEDOUT' > sf-compat/sf-etime.h
  '';

  configurePhase = ''
    runHook preConfigure
    export PATH="${nativeTools}/bin:${p.native-ld}/bin:$PATH"
    export PKG_CONFIG_PATH="${libdrm}/lib/pkgconfig:${lib.getDev zlib}/lib/pkgconfig:${lib.getDev expat}/lib/pkgconfig"
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"${wlEnv}

    cat > siliconforest-cross.ini <<EOF
    [binaries]
    c = '${tc}/bin/${triple}-clang'
    cpp = '${tc}/bin/${triple}-clang++'
    objc = '${tc}/bin/${triple}-clang'
    objcpp = '${tc}/bin/${triple}-clang++'
    ar = '${tc}/bin/${triple}-ar'
    strip = '${tc}/bin/${triple}-strip'
    pkg-config = '${pkgs.pkg-config}/bin/pkg-config'
    install_name_tool = '${tc}/bin/${triple}-install_name_tool'

    [built-in options]
    c_args = [${list cflags}, '-I$PWD/sf-compat'${etimeArg}]
    cpp_args = [${list cflags}, '-I$PWD/sf-compat'${etimeArg}, '-nostdinc++', '-I${cxx}/usr/include/c++/v1']
    objc_args = [${list cflags}, '-I$PWD/sf-compat'${etimeArg}]
    c_link_args = [${list ldflags}, '-lSystem']
    objc_link_args = [${list ldflags}, '-lSystem']
    cpp_link_args = [${list ldflags}, '-L${cxx}/usr/lib', '-L${cxxabi}/usr/lib', '-lc++', '-lc++abi', '-lSystem']

    [host_machine]
    system = 'darwin'
    subsystem = 'macos'
    cpu_family = 'aarch64'
    cpu = 'aarch64'
    endian = 'little'

    [properties]
    needs_exe_wrapper = true
    EOF
    sed -i 's/^    //' siliconforest-cross.ini

    meson setup build \
      --cross-file siliconforest-cross.ini \
      --prefix=$out/usr \
      --libdir=lib \
      --buildtype=release \
      -Ddefault_library=shared \
      -Dgallium-drivers=asahi,softpipe \
      -Dvulkan-drivers=${lib.optionalString vulkan "asahi"} \
      -Dplatforms=${lib.optionalString wayland "wayland"} \
      -Degl-native-platform=surfaceless \
      -Dopengl=true \
      -Dgles1=disabled \
      -Dgles2=enabled \
      -Dglx=disabled \
      -Degl=enabled \
      -Dgbm=disabled \
      -Dllvm=disabled \
      -Ddraw-use-llvm=false \
      -Dmesa-clc=system \
      -Dprecomp-compiler=system \
      -Dgallium-va=disabled \
      -Dgallium-rusticl=false \
      -Dglvnd=disabled \
      -Dlmsensors=disabled \
      -Dzstd=disabled \
      -Dvalgrind=disabled \
      -Dlibunwind=disabled \
      -Dxlib-lease=disabled \
      -Dbuild-tests=false
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    ninja -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    ninja -C build install
    # image layout: everything under /usr/lib, like SiliconForest's own Mesa packages
    INT=${tc}/bin/${triple}-install_name_tool
    dylibs=$(find "$out/usr/lib" -maxdepth 1 -name "*.dylib" -not -type l)
    for d in $dylibs; do "$INT" -id "/usr/lib/$(basename "$d")" "$d" || true; done
    for f in $dylibs; do
      for d in $dylibs; do
        b=$(basename "$d")
        "$INT" -change "@rpath/$b" "/usr/lib/$b" "$f" 2>/dev/null || true
        "$INT" -change "$out/usr/lib/$b" "/usr/lib/$b" "$f" 2>/dev/null || true
      done
    done
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;
} // lib.optionalAttrs vulkan {
  # The generated entrypoint tables take weak references to every entrypoint, which Mach-O can
  # only leave unresolved through dynamic lookup (SiliconForest's mesa-powervr.nix does the same);
  # postInstall lists what is left unresolved.
  preConfigure = ''
    substituteInPlace src/vulkan/meson.build \
      --replace-fail "vulkan_icd_link_args = []" \
                     "vulkan_icd_link_args = host_machine.system() == 'darwin' ? ['-Wl,-undefined,dynamic_lookup'] : []"
  '';
  # the ICD names the driver where the image keeps it; a staged copy brings its own JSON
  postInstall = ''
    for icd in "$out"/usr/share/vulkan/icd.d/*.json; do
      sed -i "s|\"library_path\": \".*/\([^/]*\.dylib\)\"|\"library_path\": \"/usr/lib/\1\"|" "$icd"
    done
    ${tc}/bin/${triple}-nm -m -u "$out"/usr/lib/libvulkan_asahi.dylib > "$out"/usr/share/vulkan/libvulkan_asahi.undefined.txt
  '';
})
